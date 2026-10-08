import Foundation

public enum ReferenceLayers {
  public static func load(_ feed: FeedKind) throws -> Snapshot {
    let file: String
    let directory: String
    let lines: Bool
    switch feed {
    case .datacenters: (file, directory, lines) = ("datacenters.geojsonl", "datacenters", true)
    case .dams: (file, directory, lines) = ("dams.geojsonl", "dams", true)
    case .cables:
      (file, directory, lines) = ("cable-geo.json", "telegeography_submarine_cables", false)
    default: throw FeedError.malformed
    }
    guard
      let url = Bundle.module.url(
        forResource: file, withExtension: nil, subdirectory: "Resources/\(directory)")
    else { throw FeedError.coverage("Bundled reference data is missing.") }
    let data = try Data(contentsOf: url)
    let rows: [[String: Any]]
    if lines {
      guard let text = String(data: data, encoding: .utf8) else { throw FeedError.malformed }
      rows = try text.split(separator: "\n").map {
        guard let row = try JSONSerialization.jsonObject(with: Data($0.utf8)) as? [String: Any]
        else { throw FeedError.malformed }
        return row
      }
    } else {
      guard let root = try JSONSerialization.jsonObject(with: data) as? [String: Any],
        let features = root["features"] as? [[String: Any]]
      else { throw FeedError.malformed }
      rows = features
    }
    if feed == .cables {
      guard
        let landingURL = Bundle.module.url(
          forResource: "landing-point-geo.json", withExtension: nil,
          subdirectory: "Resources/telegeography_submarine_cables"),
        let landingRoot = try JSONSerialization.jsonObject(with: Data(contentsOf: landingURL))
          as? [String: Any], let landings = landingRoot["features"] as? [[String: Any]]
      else { throw FeedError.malformed }
      // Separate identifier namespaces for cable systems and their landing stations.
      let systems = rows.map { row -> [String: Any] in
        var row = row
        let p = row["properties"] as? [String: Any] ?? [:]
        row["id"] = "system:" + String(describing: row["id"] ?? p["id"] ?? "unknown")
        return row
      }
      let stations = landings.map { row -> [String: Any] in
        var row = row
        let p = row["properties"] as? [String: Any] ?? [:]
        row["id"] = "landing:" + String(describing: row["id"] ?? p["id"] ?? "unknown")
        return row
      }
      return parse(systems + stations, feed: feed)
    }
    return parse(rows, feed: feed)
  }
  public static func geometry(_ value: Any?) -> [[Coordinate]] {
    guard let geometry = value as? [String: Any], let type = geometry["type"] as? String else {
      return []
    }
    func line(_ value: Any?) -> [Coordinate] {
      guard let pairs = value as? [[Any]], pairs.count <= 50_000 else { return [] }
      let points = pairs.compactMap { pair -> Coordinate? in
        guard pair.count >= 2, let lon = Parsers.jsonNumber(pair[0]),
          let lat = Parsers.jsonNumber(pair[1])
        else { return nil }
        return Coordinate(latitude: lat, longitude: lon)
      }
      return points.count == pairs.count ? points : []
    }
    switch type {
    case "Point": return [line([geometry["coordinates"] as? [Any] ?? []])].filter { !$0.isEmpty }
    case "LineString": return [line(geometry["coordinates"])].filter { $0.count >= 2 }
    case "Polygon", "MultiLineString":
      return (geometry["coordinates"] as? [Any] ?? []).map(line).filter { $0.count >= 2 }
    case "MultiPolygon":
      return (geometry["coordinates"] as? [[Any]] ?? []).flatMap {
        $0.map(line).filter { $0.count >= 2 }
      }
    default: return []
    }
  }
  public static func parse(_ rows: [[String: Any]], feed: FeedKind) -> Snapshot {
    var seen = Set<String>()
    var records: [GeoRecord] = []
    var rejected = 0
    for row in rows {
      let paths = geometry(row["geometry"])
      let points = paths.flatMap { $0 }
      guard !points.isEmpty, let p = row["properties"] as? [String: Any] else {
        rejected += 1
        continue
      }
      let tags = p["tags"] as? [String: Any] ?? p
      // A source vertex stays on the actual geometry, including date-line cables.
      // A bounding-box midpoint could falsely place a Pacific cable in the Atlantic.
      let coordinate = points[0]
      let name = SourceText.clean(tags["name"] ?? p["name"], limit: 200) ?? "Unnamed mapped feature"
      let rawID =
        row["id"].map { String(describing: $0) } ?? p["id"].map { String(describing: $0) } ?? name
      let id = "\(feed.rawValue):\(rawID)"
      guard seen.insert(id).inserted else {
        rejected += 1
        continue
      }
      var details = [
        "Evidence": "Bundled reference snapshot; operational state and acquisition date unknown.",
        "Location method": points.count == 1 ? "Reported point" : "First reported geometry vertex",
        "Data license": feed == .cables
          ? "CC BY-NC-SA 3.0 (non-commercial)" : "ODbL 1.0",
      ]
      for key in [
        "operator", "capacity", "power", "waterway", "telecom", "start_date",
        "plant:output:electricity",
      ] {
        if let value = SourceText.clean(tags[key]) { details[key] = value }
      }
      if feed == .cables {
        details["Snapshot date"] = "2026-05-24"
        details["Source URL"] = "https://www.submarinecablemap.com/"
      }
      records.append(
        .init(
          id: id, feed: feed, label: name, coordinate: coordinate, altitude: nil, observedAt: nil,
          source: feed == .cables ? "TeleGeography" : "OpenStreetMap",
          details: details, footprint: paths.first?.count ?? 0 > 2 ? paths.first! : [], paths: paths
        ))
    }
    return Snapshot(
      records: records, rejected: rejected, source: "Bundled \(feed.name)",
      notices: ["Reference data, not a live feed. \(feed.credit)"])
  }
}
