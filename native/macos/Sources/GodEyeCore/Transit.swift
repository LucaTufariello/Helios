import Foundation

public struct TransitOperator: Decodable, Identifiable, Sendable {
  public let id: String, name: String, url: String, attribution: String, license: String,
    licenseUrl: String
  public let lat: Double, lon: Double, radius: Double
  public let headers: [String: String]
  public var center: Coordinate { Coordinate(latitude: lat, longitude: lon)! }
  public static let catalog: [TransitOperator] = {
    guard
      let url = Bundle.module.url(
        forResource: "transit-feeds", withExtension: "json", subdirectory: "Resources"),
      let data = try? Data(contentsOf: url),
      let items = try? JSONDecoder().decode([TransitOperator].self, from: data)
    else { return [] }
    return items.filter {
      URL(string: $0.url)?.scheme == "https"
        && Coordinate(latitude: $0.lat, longitude: $0.lon) != nil
    }
  }()
}
public enum TransitParser {
  public static func parse(_ data: Data, provider: TransitOperator, now: Date = Date()) throws
    -> Snapshot
  {
    let root = try Proto.read(Array(data))
    guard let header = root.first(where: { $0.number == 1 })?.payload,
      try Proto.read(header).first(where: { $0.number == 1 })?.payload != nil
    else { throw FeedError.malformed }
    var records: [GeoRecord] = []
    var seen = Set<String>()
    var rejected = 0
    for entry in root where entry.number == 2 {
      guard let bytes = entry.payload else { throw FeedError.malformed }
      let entity = try Proto.read(bytes)
      if entity.first(where: { $0.number == 2 })?.integer == 1 { continue }
      guard let vehicle = entity.first(where: { $0.number == 4 })?.payload else { continue }
      let fields = try Proto.read(vehicle)
      guard let position = fields.first(where: { $0.number == 2 })?.payload else {
        rejected += 1
        continue
      }
      let pos = try Proto.read(position)
      guard let latBits = pos.first(where: { $0.number == 1 })?.fixed,
        let lonBits = pos.first(where: { $0.number == 2 })?.fixed,
        let coordinate = Coordinate(
          latitude: Double(Float(bitPattern: latBits)),
          longitude: Double(Float(bitPattern: lonBits)))
      else {
        rejected += 1
        continue
      }
      let descriptor = try Proto.read(fields.first(where: { $0.number == 8 })?.payload ?? [])
      func text(_ rows: [Proto.Field], _ field: Int) -> String? {
        rows.first(where: { $0.number == field })?.payload.flatMap {
          String(bytes: $0, encoding: .utf8)
        }.flatMap { SourceText.clean($0, limit: 160) }
      }
      guard let raw = text(descriptor, 1) ?? text(entity, 1) else {
        rejected += 1
        continue
      }
      let id = "transit:\(provider.id):\(raw)"
      guard seen.insert(id).inserted else {
        rejected += 1
        continue
      }
      let timestamp = fields.first(where: { $0.number == 5 })?.integer.map {
        Date(timeIntervalSince1970: Double($0))
      }
      // Do not replace a missing vehicle fix time with the header's fetch time.
      if let timestamp,
        now.timeIntervalSince(timestamp) > 180 || timestamp.timeIntervalSince(now) > 120
      {
        rejected += 1
        continue
      }
      let trip = try Proto.read(fields.first(where: { $0.number == 1 })?.payload ?? [])
      var details = [
        "Vehicle ID": raw, "Operator": provider.name, "Route": text(trip, 5) ?? "Unknown",
        "Data license": provider.license, "Source URL": provider.licenseUrl,
        "Position basis": "Operator-reported GTFS-realtime fix",
      ]
      if let speed = pos.first(where: { $0.number == 5 })?.fixed.map({
        Double(Float(bitPattern: $0))
      }), speed.isFinite, speed >= 0 {
        details["Speed"] = String(format: "%.1f km/h", speed * 3.6)
      }
      records.append(
        .init(
          id: id, feed: .transit, label: "\(provider.name) · \(text(descriptor, 2) ?? raw)",
          coordinate: coordinate, altitude: nil, observedAt: timestamp,
          source: provider.attribution, details: details))
    }
    return Snapshot(
      records: records, rejected: rejected, source: provider.attribution,
      notices: [
        "Vehicle fixes older than 3 minutes are excluded. Missing observation times stay unknown."
      ], retrievedAt: now)
  }
}
