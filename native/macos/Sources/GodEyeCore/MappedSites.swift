import Foundation

public enum MappedSites {
  public static func query(feed: FeedKind, bounds: GeoBounds) throws -> String {
    guard feed == .installations || feed == .energy else { throw FeedError.malformed }
    let span = feed == .energy ? 2.0 : 10.0
    guard bounds.north - bounds.south <= span, bounds.east - bounds.west <= span else {
      throw FeedError.coverage("Zoom in: maximum query span is \(Int(span))°.")
    }
    let filters =
      feed == .installations
      ? [
        "nwr[\"military\"=\"airfield\"]", "nwr[\"military\"=\"naval_base\"]",
        "nwr[\"military\"=\"range\"]", "nwr[\"military\"=\"barracks\"]",
        "nwr[\"military\"=\"base\"]",
        "nwr[\"landuse\"=\"military\"]",
      ]
      : [
        "node[\"power\"=\"plant\"]", "way[\"power\"=\"plant\"]", "node[\"power\"=\"substation\"]",
        "way[\"power\"=\"substation\"]", "node[\"industrial\"=\"refinery\"]",
        "way[\"industrial\"=\"refinery\"]",
      ]
    let cap = feed == .installations ? 700 : 400
    return "[out:json][timeout:20];(" + filters.map { "\($0)(\(bounds.query));" }.joined()
      + ");out center tags geom \(cap);"
  }
  public static func parse(_ data: Data, feed: FeedKind, bounds: GeoBounds) throws -> Snapshot {
    guard feed == .installations || feed == .energy,
      let root = try JSONSerialization.jsonObject(with: data) as? [String: Any],
      let rows = root["elements"] as? [[String: Any]]
    else { throw FeedError.malformed }
    // Overpass timeout/partial responses can contain elements and still be incomplete.
    if let remark = root["remark"] as? String, !remark.isEmpty {
      throw FeedError.coverage("Map provider returned an incomplete response. Try a smaller view.")
    }
    let cap = feed == .installations ? 700 : 400
    var records: [GeoRecord] = []
    var seen = Set<String>()
    var rejected = 0
    for row in rows.prefix(cap) {
      guard let type = row["type"] as? String, ["node", "way", "relation"].contains(type),
        let number = Parsers.jsonNumber(row["id"]), number > 0, number <= 9_007_199_254_740_991,
        number.rounded() == number,
        let tags = row["tags"] as? [String: Any]
      else {
        rejected += 1
        continue
      }
      let military = tags["military"] as? String
      let power = tags["power"] as? String
      let kind: String
      if feed == .installations {
        let kinds = [
          "airfield": "Military airfield", "naval_base": "Naval base", "range": "Firing range",
          "barracks": "Barracks", "base": "Military base",
        ]
        if let military, let name = kinds[military] {
          kind = name
        } else if tags["landuse"] as? String == "military" {
          kind = "Military land"
        } else {
          rejected += 1
          continue
        }
      } else {
        guard type != "relation" else {
          rejected += 1
          continue
        }
        if power == "plant" {
          kind = "Power plant"
        } else if power == "substation" {
          kind = "Substation"
        } else if tags["industrial"] as? String == "refinery" {
          kind = "Refinery"
        } else {
          rejected += 1
          continue
        }
      }
      let id = "osm:\(type):\(String(format: "%.0f", number))"
      func point(_ row: [String: Any]) -> Coordinate? {
        guard let lat = Parsers.jsonNumber(row["lat"]), let lon = Parsers.jsonNumber(row["lon"])
        else { return nil }
        return Coordinate(latitude: lat, longitude: lon)
      }
      let geometry = row["geometry"] as? [[String: Any]] ?? []
      let validGeometry = geometry.count <= 400 ? geometry.compactMap(point) : []
      let closed =
        validGeometry.count >= 4 && validGeometry.count == geometry.count
        && validGeometry.first == validGeometry.last
      let footprint = closed ? validGeometry : []
      var coordinate = point(row)
      var method = "Reported OSM point"
      if coordinate == nil, let center = row["center"] as? [String: Any] {
        coordinate = point(center)
        method = "OSM representative center"
      }
      if coordinate == nil, let b = row["bounds"] as? [String: Any],
        let south = Parsers.jsonNumber(b["minlat"]), let west = Parsers.jsonNumber(b["minlon"]),
        let north = Parsers.jsonNumber(b["maxlat"]), let east = Parsers.jsonNumber(b["maxlon"]),
        let box = GeoBounds(south: south, west: west, north: north, east: east),
        box.north - box.south <= 10, box.east - box.west <= 10
      {
        coordinate = Coordinate(latitude: (south + north) / 2, longitude: (west + east) / 2)
        method = "Mapped bounds midpoint; not an entrance"
      }
      if coordinate == nil, !validGeometry.isEmpty, validGeometry.count == geometry.count {
        let lats = validGeometry.map(\.latitude)
        let lons = validGeometry.map(\.longitude)
        if lons.max()! - lons.min()! <= 10, lats.max()! - lats.min()! <= 10 {
          coordinate = Coordinate(
            latitude: (lats.min()! + lats.max()!) / 2, longitude: (lons.min()! + lons.max()!) / 2)
          method = "Mapped geometry midpoint; not an entrance"
        }
      }
      guard let coordinate else {
        rejected += 1
        continue
      }
      // Ways that intersect the viewport can have a representative point just outside it.
      // Keep those ways (their geometry is relevant), without claiming the point is in view.
      let intersects = bounds.contains(coordinate) || validGeometry.contains(where: bounds.contains)
      guard intersects else { continue }
      guard seen.insert(id).inserted else {
        rejected += 1
        continue
      }
      var details = [
        "Type": kind, "Location method": method,
        "Source URL": "https://www.openstreetmap.org/\(type)/\(String(format: "%.0f",number))",
        "Evidence": "Community-mapped context; operational state unknown",
        "Footprint": footprint.isEmpty
          ? "Unavailable or unsupported geometry" : "Selected mapped outline; reference ellipsoid",
      ]
      for key in [
        "operator", "power", "plant:source", "plant:output:electricity", "military", "landuse",
        "industrial",
      ] {
        if let value = SourceText.clean(tags[key]) { details[key] = value }
      }
      records.append(
        .init(
          id: "\(feed.rawValue):\(id)", feed: feed,
          label: SourceText.clean(tags["name"] ?? tags["name:en"]) ?? kind, coordinate: coordinate,
          altitude: nil, observedAt: nil, source: "OpenStreetMap", details: details,
          footprint: footprint))
    }
    var notices = [
      String(
        format: "Mapped context · area %.3f°, %.3f° to %.3f°, %.3f°", bounds.south, bounds.west,
        bounds.north, bounds.east)
    ]
    if rows.count >= cap {
      notices.append("Result limit \(cap) reached. Coverage may be incomplete; zoom in.")
    }
    return Snapshot(
      records: records, rejected: rejected, source: "OpenStreetMap / Overpass", notices: notices,
      retrievedAt: Date())
  }
}
