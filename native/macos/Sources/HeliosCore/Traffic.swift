import CryptoKit
import Foundation

/// TomTom's relative vector-flow tiles. Unknown flow values never become green roads.
public enum TrafficParser {
  private enum Value {
    case text(String)
    case number(Double)
    case boolean(Bool)
    case unknown
  }
  private static func packed(_ bytes: [UInt8]) throws -> [UInt64] {
    var reader = Proto(bytes: bytes)
    var result: [UInt64] = []
    while reader.offset < bytes.count {
      guard result.count < 100_000 else { throw FeedError.malformed }
      result.append(try reader.varint())
    }
    return result
  }
  public static func parse(_ data: Data, tile: TileID, retrievedAt: Date = Date()) throws
    -> Snapshot
  {
    let root = try Proto.read(Array(data))
    var records: [GeoRecord] = []
    var rejected = 0
    var identifiers: Set<String> = []
    for layerField in root where layerField.number == 3 {
      guard let payload = layerField.payload else { throw FeedError.malformed }
      let layer = try Proto.read(payload)
      guard let name = layer.first(where: { $0.number == 1 })?.payload,
        String(bytes: name, encoding: .utf8) == "Traffic flow"
      else { continue }
      guard layer.first(where: { $0.number == 15 })?.integer == 2 else { throw FeedError.malformed }
      let extent = Double(layer.first(where: { $0.number == 5 })?.integer ?? 4096)
      guard extent >= 256, extent <= 65536 else { throw FeedError.malformed }
      let keys = layer.filter { $0.number == 3 }.map {
        String(bytes: $0.payload ?? [], encoding: .utf8) ?? ""
      }
      let values: [Value] = try layer.filter { $0.number == 4 }.map { field in
        let fields = try Proto.read(field.payload ?? [])
        if let value = fields.first(where: { $0.number == 1 })?.payload {
          return .text(String(bytes: value, encoding: .utf8) ?? "")
        }
        if let value = fields.first(where: { $0.number == 3 })?.fixed64 {
          return .number(Double(bitPattern: value))
        }
        if let value = fields.first(where: { $0.number == 2 })?.fixed {
          return .number(Double(Float(bitPattern: value)))
        }
        if let value = fields.first(where: { $0.number == 7 })?.integer {
          return .boolean(value == 1)
        }
        return .unknown
      }
      for feature in layer where feature.number == 2 {
        do {
          let fields = try Proto.read(feature.payload ?? [])
          guard fields.first(where: { $0.number == 3 })?.integer == 2 else { continue }
          let tags = try packed(fields.first(where: { $0.number == 2 })?.payload ?? [])
          guard tags.count % 2 == 0 else { throw FeedError.malformed }
          var properties: [String: Value] = [:]
          for i in stride(from: 0, to: tags.count, by: 2) {
            guard tags[i] < keys.count, tags[i + 1] < values.count else {
              throw FeedError.malformed
            }
            properties[keys[Int(tags[i])]] = values[Int(tags[i + 1])]
          }
          let closed: Bool
          switch properties["road_closure"] {
          case .boolean(true), .text("true"): closed = true
          default: closed = false
          }
          var ratio: Double?
          if case .number(let value) = properties["traffic_level"], value.isFinite,
            (0...1).contains(value)
          {
            ratio = value
          }
          guard ratio != nil || closed else {
            rejected += 1
            continue
          }
          let geometry = fields.first(where: { $0.number == 4 })?.payload ?? []
          let commands = try packed(geometry)
          var paths: [[Coordinate]] = []
          var path: [Coordinate] = []
          var x: Int64 = 0
          var y: Int64 = 0
          var cursor = 0
          while cursor < commands.count {
            let command = commands[cursor]
            cursor += 1
            let kind = command & 7
            let count = command >> 3
            guard kind == 1 || kind == 2, count > 0, count <= 50_000,
              count * 2 <= commands.count - cursor
            else { throw FeedError.malformed }
            for _ in 0..<count {
              if kind == 1 {
                if path.count > 1 { paths.append(path) }
                path = []
              }
              func delta(_ value: UInt64) throws -> Int64 {
                guard value <= UInt32.max else { throw FeedError.malformed }
                return Int64(value >> 1) ^ -Int64(value & 1)
              }
              x += try delta(commands[cursor])
              y += try delta(commands[cursor + 1])
              cursor += 2
              guard abs(x) <= Int64(extent * 2), abs(y) <= Int64(extent * 2) else {
                throw FeedError.malformed
              }
              let n = Double(1 << tile.zoom)
              let longitude = (Double(tile.x) + Double(x) / extent) / n * 360 - 180
              let latitude =
                atan(sinh(Double.pi * (1 - 2 * (Double(tile.y) + Double(y) / extent) / n))) * 180
                / Double.pi
              guard let point = Coordinate(latitude: latitude, longitude: longitude) else {
                throw FeedError.malformed
              }
              path.append(point)
            }
          }
          if path.count > 1 { paths.append(path) }
          guard let point = paths.first?.first else { throw FeedError.malformed }
          // Providers can reorder features between refreshes. Selection must stay on the same geometry.
          let digest = SHA256.hash(data: Data(geometry)).map { String(format: "%02x", $0) }.joined()
          let identifier = "traffic:\(tile.zoom):\(tile.x):\(tile.y):\(Int(extent)):\(digest)"
          guard identifiers.insert(identifier).inserted else {
            rejected += 1
            continue
          }
          let road: String
          if case .text(let name) = properties["road_type"] {
            road = SourceText.clean(name) ?? "Road segment"
          } else {
            road = "Road segment"
          }
          var details = [
            "Road closed": closed ? "Yes" : "Not reported",
            "Position basis": "TomTom reported road geometry",
            "Source URL": "https://www.tomtom.com/",
            "Acquisition time": "Unknown; retrieval time is separate",
          ]
          if let ratio {
            details["Relative flow"] = String(ratio)
            details["Flow description"] = String(format: "%.0f%% of free-flow speed", ratio * 100)
          }
          records.append(
            .init(
              id: identifier, feed: .traffic, label: road,
              coordinate: point, altitude: nil, observedAt: nil, source: "© TomTom",
              details: details, paths: paths))
        } catch { rejected += 1 }
      }
    }
    return Snapshot(
      records: records, rejected: rejected, source: "© TomTom",
      notices: [
        "Relative road-flow estimates from provider tiles; acquisition time unavailable. Green ≥85%, amber ≥55%, red below 55% or closure."
      ], retrievedAt: retrievedAt)
  }
}

public actor TrafficSource {
  private let transport = Transport()
  private var cache: [TileID: Snapshot] = [:]
  private var credential = ""
  private var day = ""
  private var requests = 0
  public init() {}
  public func shutdown() { transport.session.invalidateAndCancel() }
  public func snapshot(camera: Camera, aspect: Double, key: String) async throws -> Snapshot {
    guard (16...128).contains(key.count),
      key.allSatisfy({ $0.isASCII && ($0.isLetter || $0.isNumber || $0 == "-" || $0 == "_") })
    else {
      throw FeedError.coverage(
        "Add a TomTom API key in Provider Settings to load real traffic flow.")
    }
    let bounds = try GeoBounds.viewport(camera: camera, aspect: aspect, maximumSpan: 0.05)
    guard abs(bounds.south) < 85, abs(bounds.north) < 85 else {
      throw FeedError.coverage("Traffic tiles do not cover the poles.")
    }
    if credential != key {
      cache.removeAll()
      credential = key
    }
    let currentDay = String(ISO8601DateFormatter().string(from: Date()).prefix(10))
    if currentDay != day {
      day = currentDay
      requests = 0
    }
    let nw = TileID.containing(
      Coordinate(latitude: bounds.north, longitude: bounds.west)!, zoom: 12)
    let se = TileID.containing(
      Coordinate(latitude: bounds.south, longitude: bounds.east)!, zoom: 12)
    guard nw.x <= se.x, nw.y <= se.y, (se.x - nw.x + 1) * (se.y - nw.y + 1) <= 9 else {
      throw FeedError.coverage("Zoom in for a bounded traffic request.")
    }
    var snapshots: [Snapshot] = []
    var failures = 0
    for y in nw.y...se.y {
      for x in nw.x...se.x {
        try Task.checkCancellation()
        let tile = TileID(x: x, y: y, zoom: 12)!
        if let cached = cache[tile], let time = cached.retrievedAt,
          Date().timeIntervalSince(time) < 120
        {
          snapshots.append(cached)
          continue
        }
        guard requests < 1000 else {
          throw FeedError.coverage(
            "The 1,000-tile daily budget for this app session has been reached. Check your provider quota before restarting."
          )
        }
        requests += 1
        var url = URLComponents(
          string: "https://api.tomtom.com/traffic/map/4/tile/flow/relative/12/\(x)/\(y).pbf")!
        url.queryItems = [.init(name: "key", value: key), .init(name: "margin", value: "0")]
        do {
          let snapshot = try TrafficParser.parse(
            await transport.data(url.url!, maximumBytes: 2 * 1024 * 1024, allowRedirects: false),
            tile: tile)
          try Task.checkCancellation()
          if cache.count >= 64,
            let oldest = cache.min(by: {
              ($0.value.retrievedAt ?? .distantPast) < ($1.value.retrievedAt ?? .distantPast)
            })?.key
          {
            cache.removeValue(forKey: oldest)
          }
          cache[tile] = snapshot
          snapshots.append(snapshot)
        } catch is CancellationError { throw CancellationError() } catch { failures += 1 }
      }
    }
    guard !snapshots.isEmpty else {
      throw FeedError.coverage(
        "TomTom traffic tiles could not be loaded. Check your API key, quota and connection.")
    }
    return Snapshot(
      records: snapshots.flatMap(\.records), rejected: snapshots.reduce(0) { $0 + $1.rejected },
      source: "© TomTom",
      notices: [
        "Bounded local view, at most nine tiles. \(failures) tile requests failed. \(requests)/1,000 requests used today in this session. No synthetic traffic or absolute-speed inference."
      ], retrievedAt: snapshots.compactMap(\.retrievedAt).min())
  }
}
