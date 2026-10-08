import Foundation
import ImageIO
import simd

/// Source RGB codes are data. Never color-convert, gamma-correct or premultiply them.
public struct ElevationTile: Sendable {
  public let id: TileID
  public let heights: [Float]
  public let retrievedAt: Date
  public init(id: TileID, heights: [Float], retrievedAt: Date = Date()) throws {
    guard heights.count == 256 * 256,
      heights.allSatisfy({ $0.isFinite && (-12_000...12_000).contains($0) })
    else { throw FeedError.malformed }
    self.id = id
    self.heights = heights
    self.retrievedAt = retrievedAt
  }
  public static func decode(_ data: Data, id: TileID) throws -> ElevationTile {
    guard data.count <= 1024 * 1024 else { throw FeedError.oversized }
    guard let source = CGImageSourceCreateWithData(data as CFData, nil),
      CGImageSourceGetCount(source) == 1,
      let properties = CGImageSourceCopyPropertiesAtIndex(source, 0, nil) as? [String: Any],
      properties[kCGImagePropertyPixelWidth as String] as? Int == 256,
      properties[kCGImagePropertyPixelHeight as String] as? Int == 256,
      let image = CGImageSourceCreateImageAtIndex(source, 0, nil),
      image.bitsPerComponent == 8, image.colorSpace?.model == .rgb,
      image.bitsPerPixel == 24 || image.bitsPerPixel == 32,
      let raw = image.dataProvider?.data
    else { throw FeedError.malformed }
    let bytes = raw as Data
    let stride = image.bitsPerPixel / 8
    // ImageIO PNG decodes channels in RGB[A] order, without a bitmap context conversion.
    guard image.alphaInfo == .none || image.alphaInfo == .last || image.alphaInfo == .noneSkipLast,
      bytes.count >= image.bytesPerRow * 256, image.bytesPerRow >= 256 * stride
    else { throw FeedError.malformed }
    var heights: [Float] = []
    heights.reserveCapacity(65_536)
    for y in 0..<256 {
      for x in 0..<256 {
        let offset = y * image.bytesPerRow + x * stride
        if image.alphaInfo == .last && bytes[offset + 3] != 255 { throw FeedError.malformed }
        heights.append(
          Float(bytes[offset]) * 256 + Float(bytes[offset + 1]) + Float(bytes[offset + 2]) / 256
            - 32768)
      }
    }
    return try .init(id: id, heights: heights)
  }
  public func height(u: Double, v: Double) -> Double {
    let x = min(255, max(0, u * 256 - 0.5))
    let y = min(255, max(0, v * 256 - 0.5))
    let ix = Int(x)
    let iy = Int(y)
    let nx = min(255, ix + 1)
    let ny = min(255, iy + 1)
    let dx = x - Double(ix)
    let dy = y - Double(iy)
    let a = Double(heights[iy * 256 + ix]) * (1 - dx) + Double(heights[iy * 256 + nx]) * dx
    let b = Double(heights[ny * 256 + ix]) * (1 - dx) + Double(heights[ny * 256 + nx]) * dx
    return a * (1 - dy) + b * dy
  }
  public static func url(_ id: TileID) -> URL {
    URL(
      string:
        "https://s3.amazonaws.com/elevation-tiles-prod/terrarium/\(id.zoom)/\(id.x)/\(id.y).png")!
  }
}

public struct TerrainSurface: Sendable {
  public private(set) var tiles: [TileID: ElevationTile] = [:]
  public init() {}
  public mutating func insert(_ tile: ElevationTile) {
    tiles[tile.id] = tile
    while tiles.count > 96,
      let oldest = tiles.min(by: { $0.value.retrievedAt < $1.value.retrievedAt })?.key
    {
      tiles.removeValue(forKey: oldest)
    }
  }
  public mutating func retain(_ desired: Set<TileID>) {
    tiles = tiles.filter { desired.contains($0.key) }
  }
  public func height(at coordinate: Coordinate) -> Double? {
    for zoom in stride(from: 14, through: 0, by: -1) {
      let id = TileID.containing(coordinate, zoom: zoom)
      if let tile = tiles[id] {
        let n = Double(1 << zoom)
        let x = (coordinate.longitude + 180) / 360 * n
        let lat = min(85.05112878, max(-85.05112878, coordinate.latitude)) * .pi / 180
        let y = (1 - log(tan(lat) + 1 / cos(lat)) / .pi) / 2 * n
        return tile.height(u: x - floor(x), v: y - floor(y))
      }
    }
    return nil
  }
  public func groundHeight(at coordinate: Coordinate) -> Double? {
    // Ocean imagery sits at sea level; negative source depths are retained in the tile data.
    height(at: coordinate).map { max(0, $0) }
  }
  public func pick(_ ray: SceneRay) -> SIMD3<Double>? {
    var nearest = Double.infinity
    for tile in tiles.values {
      let center = tile.id.coordinate(u: 0.5, v: 0.5).ecef()
      let radius = simd_distance(center, tile.id.coordinate(u: 0, v: 0).ecef()) + 12_000
      let projection = simd_dot(center - ray.origin, ray.direction)
      if projection + radius < 0
        || simd_length(ray.origin + max(0, projection) * ray.direction - center) > radius
      {
        continue
      }
      let divisions = 32
      func point(_ x: Int, _ y: Int) -> SIMD3<Double> {
        let u = Double(x) / Double(divisions)
        let v = Double(y) / Double(divisions)
        return tile.id.coordinate(u: u, v: v).ecef(altitude: max(0, tile.height(u: u, v: v)))
      }
      for y in 0..<divisions {
        for x in 0..<divisions {
          let a = point(x, y)
          let b = point(x + 1, y)
          let c = point(x, y + 1)
          let d = point(x + 1, y + 1)
          for distance in [ray.triangle(a, c, b), ray.triangle(b, c, d)].compactMap({ $0 })
          where distance < nearest {
            nearest = distance
          }
        }
      }
    }
    return nearest.isFinite ? ray.origin + ray.direction * nearest : nil
  }
}
