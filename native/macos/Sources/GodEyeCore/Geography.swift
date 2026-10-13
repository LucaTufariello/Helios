import Foundation
import GodEyeSpatial
import simd

public struct Coordinate: Codable, Hashable, Sendable {
  public let latitude: Double
  public let longitude: Double
  public init?(latitude: Double, longitude: Double) {
    guard latitude.isFinite, longitude.isFinite, abs(latitude) <= 90, abs(longitude) <= 180 else {
      return nil
    }
    self.latitude = latitude
    self.longitude = longitude
  }
  // Decoding must go through the same validation as programmatic construction.
  public init(from decoder: any Decoder) throws {
    let values = try decoder.container(keyedBy: CodingKeys.self)
    guard
      let coordinate = Self(
        latitude: try values.decode(Double.self, forKey: .latitude),
        longitude: try values.decode(Double.self, forKey: .longitude))
    else {
      throw DecodingError.dataCorrupted(
        .init(codingPath: decoder.codingPath, debugDescription: "Invalid geographic coordinate"))
    }
    self = coordinate
  }
  public var radians: SIMD2<Double> { SIMD2(longitude * .pi / 180, latitude * .pi / 180) }
  public func ecef(altitude: Double = 0) -> SIMD3<Double> {
    var point = GEVector3()
    guard ge_wgs84_to_ecef(latitude, longitude, altitude, &point) != 0 else {
      return SIMD3(repeating: .nan)
    }
    return SIMD3(point.x, point.y, point.z)
  }
}

public struct Camera: Codable, Equatable, Sendable {
  public var center: Coordinate
  public private(set) var altitude: Double
  public private(set) var heading: Double
  public private(set) var tilt: Double
  public init(center: Coordinate, altitude: Double, heading: Double = 0, tilt: Double = 0) {
    self.center = center
    self.altitude = min(60_000_000, max(500, altitude.isFinite ? altitude : 1_500_000))
    self.heading =
      heading.isFinite
      ? ((heading.truncatingRemainder(dividingBy: 360) + 360).truncatingRemainder(dividingBy: 360))
      : 0
    self.tilt = tilt.isFinite ? min(80, max(0, tilt)) : 0
  }
  public init(from decoder: any Decoder) throws {
    let values = try decoder.container(keyedBy: CodingKeys.self)
    let center = try values.decode(Coordinate.self, forKey: .center)
    let altitude = try values.decode(Double.self, forKey: .altitude)
    guard altitude.isFinite, (500...60_000_000).contains(altitude) else {
      throw DecodingError.dataCorrupted(
        .init(codingPath: decoder.codingPath, debugDescription: "Invalid camera altitude"))
    }
    let heading = try values.decodeIfPresent(Double.self, forKey: .heading) ?? 0
    let tilt = try values.decodeIfPresent(Double.self, forKey: .tilt) ?? 0
    guard heading.isFinite, (0..<360).contains(heading), tilt.isFinite, (0...80).contains(tilt)
    else {
      throw DecodingError.dataCorrupted(
        .init(codingPath: decoder.codingPath, debugDescription: "Invalid camera orientation"))
    }
    self.init(center: center, altitude: altitude, heading: heading, tilt: tilt)
  }
  public static let initial = Camera(
    center: Coordinate(latitude: 45.4641, longitude: 9.1919)!, altitude: 1_500_000)
  public mutating func pan(dx: Double, dy: Double) {
    guard dx.isFinite, dy.isFinite, abs(dx) <= 1_000_000, abs(dy) <= 1_000_000 else { return }
    let scale = min(0.25, altitude / 6_378_137 / 8)
    let angle = heading * .pi / 180
    // Retain inverted vertical navigation; horizontal navigation uses its original direction.
    let north = -dy * cos(angle) + dx * sin(angle)
    let east = -dx * cos(angle) - dy * sin(angle)
    let lat = min(85, max(-85, center.latitude + north * scale))
    let lon = ((center.longitude + east * scale + 540).truncatingRemainder(dividingBy: 360)) - 180
    center = Coordinate(latitude: lat, longitude: lon)!
  }
  public mutating func zoom(delta: Double) {
    guard delta.isFinite else { return }
    altitude = min(60_000_000, max(500, altitude * exp(-delta * 0.03)))
  }
  public mutating func orient(heading: Double, tilt: Double) {
    guard heading.isFinite, tilt.isFinite else { return }
    self.heading =
      ((heading.truncatingRemainder(dividingBy: 360) + 360).truncatingRemainder(dividingBy: 360))
    self.tilt = min(80, max(0, tilt))
  }
  public mutating func ensureClearance(targetHeight: Double, surfaceHeight: (Coordinate) -> Double?)
  {
    // Check the displaced eye, rather than only the point the tilted camera looks at.
    for _ in 0..<4 {
      let pose = CameraPose(camera: self, targetHeight: targetHeight)
      guard let eye = SurfacePoint(ecef: pose.eye), let ground = surfaceHeight(eye.coordinate)
      else { return }
      let deficit = ground + 40 - eye.height
      guard deficit > 0 else { return }
      altitude = min(60_000_000, altitude + deficit / max(0.17, cos(tilt * .pi / 180)) + 1)
    }
  }
  public var shareURL: URL {
    var url = URLComponents()
    url.scheme = "godeye"
    url.host = "view"
    url.queryItems = [
      .init(name: "lat", value: String(center.latitude)),
      .init(name: "lon", value: String(center.longitude)),
      .init(name: "alt", value: String(altitude)),
      .init(name: "heading", value: String(heading)),
      .init(name: "tilt", value: String(tilt)),
    ]
    return url.url!
  }
  public static func fromShareURL(_ url: URL) -> Camera? {
    guard url.scheme == "godeye", url.host == "view",
      let components = URLComponents(url: url, resolvingAgainstBaseURL: false),
      let items = components.queryItems
    else { return nil }
    // Reject duplicate fields rather than accepting conflicting coordinates.
    guard Set(items.map(\.name)).count == items.count else { return nil }
    let values = Dictionary(uniqueKeysWithValues: items.map { ($0.name, $0.value ?? "") })
    guard let lat = Double(values["lat"] ?? ""), let lon = Double(values["lon"] ?? ""),
      let altitude = Double(values["alt"] ?? ""), altitude.isFinite,
      (500...60_000_000).contains(altitude),
      let coordinate = Coordinate(latitude: lat, longitude: lon)
    else { return nil }
    let heading = Double(values["heading"] ?? "0")
    let tilt = Double(values["tilt"] ?? "0")
    guard let heading, heading.isFinite, (0..<360).contains(heading), let tilt, tilt.isFinite,
      (0...80).contains(tilt)
    else { return nil }
    return Camera(center: coordinate, altitude: altitude, heading: heading, tilt: tilt)
  }
}

public struct TileID: Hashable, Sendable {
  public let x: Int, y: Int, zoom: Int
  public init?(x: Int, y: Int, zoom: Int) {
    guard (0...19).contains(zoom), (0..<(1 << zoom)).contains(y) else { return nil }
    let count = 1 << zoom
    self.x = ((x % count) + count) % count
    self.y = y
    self.zoom = zoom
  }
  public static func containing(_ coordinate: Coordinate, zoom: Int) -> TileID {
    let zoom = min(19, max(0, zoom))
    let count = Double(1 << min(19, max(0, zoom)))
    let latitude = min(85.05112878, max(-85.05112878, coordinate.latitude)) * .pi / 180
    let y = Int(floor((1 - log(tan(latitude) + 1 / cos(latitude)) / .pi) / 2 * count))
    return TileID(
      x: Int(floor((coordinate.longitude + 180) / 360 * count)), y: min(Int(count) - 1, max(0, y)),
      zoom: zoom)!
  }
  public func coordinate(u: Double, v: Double) -> Coordinate {
    let count = Double(1 << zoom)
    let lon = (Double(x) + u) / count * 360 - 180
    let lat = atan(sinh(.pi * (1 - 2 * (Double(y) + v) / count))) * 180 / .pi
    return Coordinate(latitude: lat, longitude: lon)!
  }
  public var url: URL {
    URL(
      string:
        "https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile/\(zoom)/\(y)/\(x)"
    )!
  }
  public static func visible(camera: Camera, maximumZoom: Int = 18, aspect: Double = 1) -> [TileID]
  {
    let pose = CameraPose(camera: camera)
    var coordinates = [camera.center]
    if let eye = SurfacePoint(ecef: pose.eye) { coordinates.append(eye.coordinate) }
    // Include the tilted frustum footprint and displaced eye. Coarsen until it fits a bounded grid.
    for x in [-1.0, -0.5, 0, 0.5, 1] {
      for y in [-1.0, -0.5, 0, 0.5, 1] {
        if let ray = pose.ray(x: x, y: y, aspect: aspect), let distance = ray.ellipsoidDistance(),
          let point = SurfacePoint(ecef: ray.origin + ray.direction * distance)
        {
          coordinates.append(point.coordinate)
        }
      }
    }
    let initial = min(
      min(18, max(1, maximumZoom)),
      max(
        1,
        Int(
          ceil(
            log2(
              40_075_016 * max(0.12, cos(camera.center.radians.y)) / max(75, camera.altitude * 0.15)
            )))))
    for zoom in stride(from: initial, through: 1, by: -1) {
      let center = containing(camera.center, zoom: zoom)
      let n = 1 << zoom
      let offsets = coordinates.map { point -> (Int, Int) in
        let tile = containing(point, zoom: zoom)
        var dx = tile.x - center.x
        if dx > n / 2 { dx -= n }
        if dx < -n / 2 { dx += n }
        return (dx, tile.y - center.y)
      }
      let minX = (offsets.map { $0.0 }.min() ?? 0) - 1
      let maxX = (offsets.map { $0.0 }.max() ?? 0) + 1
      let minY = (offsets.map { $0.1 }.min() ?? 0) - 1
      let maxY = (offsets.map { $0.1 }.max() ?? 0) + 1
      if (maxX - minX <= 6 && maxY - minY <= 6) || zoom == 1 {
        var result = (minY...maxY).flatMap { dy in
          (minX...maxX).compactMap { dx in TileID(x: center.x + dx, y: center.y + dy, zoom: zoom) }
        }
        if zoom < initial {
          // Retain close detail around the target and the actual eye without filling the horizon at this level.
          for point in [camera.center, SurfacePoint(ecef: pose.eye)?.coordinate ?? camera.center] {
            let near = containing(point, zoom: initial)
            result += (-1...1).flatMap { dy in
              (-1...1).compactMap { dx in TileID(x: near.x + dx, y: near.y + dy, zoom: initial) }
            }
          }
        }
        return result.uniqued()
      }
    }
    return []
  }
}
extension Array where Element: Hashable {
  fileprivate func uniqued() -> [Element] {
    var seen = Set<Element>()
    return filter { seen.insert($0).inserted }
  }
}
