import CoreFoundation
import Foundation

public enum FeedKind: String, CaseIterable, Codable, Sendable {
  case earthquakes, flights, military, atmosphere, disasters, installations, energy, satellites,
    launches, vessels, fires, transit, cameras, datacenters, dams, cables, sentinel,
    annotations, traffic
  public var name: String {
    switch self {
    case .traffic: "Road Traffic Flow"
    case .annotations: "Local Annotations"
    case .earthquakes: "Earthquakes (24h)"
    case .flights: "Live Flights"
    case .military: "Military Flights"
    case .atmosphere: "Atmosphere"
    case .disasters: "Disaster Alerts"
    case .installations: "Mapped Installations"
    case .energy: "Energy & Refining"
    case .satellites: "Satellites"
    case .launches: "Space Missions"
    case .vessels: "AIS Vessels"
    case .fires: "Thermal Detections (24h)"
    case .transit: "Public Transit"
    case .cameras: "Traffic Cameras"
    case .datacenters: "Datacenters"
    case .dams: "Dams"
    case .cables: "Submarine Cables"
    case .sentinel: "Sentinel Product Catalog"
    }
  }
  public var interval: TimeInterval {
    switch self {
    case .traffic: 120
    case .satellites: 15
    case .launches, .sentinel, .fires: 900
    case .datacenters, .dams, .cables: 86_400
    case .cameras: 1800
    case .transit: 60
    case .annotations: 86400
    case .earthquakes: 60
    case .atmosphere: 300
    case .disasters: 1800
    case .installations, .energy: 300
    default: 30
    }
  }
  public var credit: String {
    switch self {
    case .traffic: "© TomTom · provider flow estimates · API key and local view required"
    case .annotations: "User-authored notes and geometry · saved on this Mac"
    case .earthquakes: "Data courtesy of the U.S. Geological Survey"
    case .flights: "OpenSky Network (non-commercial); regional fallback: adsb.lol (ODbL 1.0)"
    case .military: "adsb.lol · ODbL 1.0"
    case .atmosphere: "Open-Meteo / CAMS · CC BY 4.0 · non-commercial · model, not a station"
    case .disasters: "GDACS · UN / EU JRC · public-domain alerts"
    case .installations, .energy, .datacenters, .dams:
      "© OpenStreetMap contributors · ODbL · community mapped, incomplete"
    case .satellites: "CelesTrak · SGP4/SDP4 propagated estimates, not observed positions"
    case .launches: "Launch Library 2 / The Space Devs · launch site, not vehicle position"
    case .vessels: "AISStream · reported AIS positions · key required"
    case .fires: "NASA FIRMS · satellite thermal detections · free MAP_KEY required"
    case .transit: "Public operator GTFS-realtime · operator attribution shown per record"
    case .cameras: "TfL / Fintraffic · public operator cameras · stills may be delayed"
    case .cables: "© TeleGeography · CC BY-NC-SA 3.0 · non-commercial data"
    case .sentinel: "Copernicus Data Space · dated product metadata, not decoded imagery"
    }
  }
}
public struct GeoRecord: Identifiable, Hashable, Sendable {
  public let id: String
  public let feed: FeedKind
  public let label: String
  public let coordinate: Coordinate
  public let altitude: Double?
  public let observedAt: Date?
  public let source: String
  public let details: [String: String]
  public let footprint: [Coordinate]
  public let paths: [[Coordinate]]
  public init(
    id: String, feed: FeedKind, label: String, coordinate: Coordinate, altitude: Double?,
    observedAt: Date?, source: String, details: [String: String], footprint: [Coordinate] = [],
    paths: [[Coordinate]] = []
  ) {
    self.id = id
    self.feed = feed
    self.label = label
    self.coordinate = coordinate
    self.altitude = altitude
    self.observedAt = observedAt
    self.source = source
    self.details = details
    self.footprint = footprint
    self.paths = paths
  }
}
public struct Snapshot: Sendable {
  public let notices: [String]
  public let retrievedAt: Date?
  public let records: [GeoRecord]
  public let rejected: Int
  public let source: String
  public init(
    records: [GeoRecord], rejected: Int, source: String, notices: [String] = [],
    retrievedAt: Date? = nil
  ) {
    self.notices = notices
    self.retrievedAt = retrievedAt
    self.records = records
    self.rejected = rejected
    self.source = source
  }
}
public enum FeedError: Error, LocalizedError, Sendable {
  case malformed
  case coverage(String)
  case http(Int)
  case oversized, insecure
  public var errorDescription: String? {
    switch self {
    case .coverage(let message): message
    case .malformed: "The source response was malformed. Previous records are retained."
    case .http(let status): "Source returned HTTP \(status)."
    case .oversized: "Source response exceeded the size limit."
    case .insecure: "Only HTTPS source requests are allowed."
    }
  }
}
public enum Parsers {
  public static func number(_ value: Any?) -> Double? {
    guard let value, !(value is NSNull) else { return nil }
    if let value = value as? NSNumber {
      guard CFGetTypeID(value) != CFBooleanGetTypeID() else { return nil }
      let number = value.doubleValue
      return number.isFinite ? number : nil
    }
    if let text = value as? String, !text.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty,
      let number = Double(text), number.isFinite
    {
      return number
    }
    return nil
  }
  public static func jsonNumber(_ value: Any?) -> Double? {
    guard value is NSNumber else { return nil }
    return number(value)
  }
  public static func earthquakes(_ data: Data) throws -> Snapshot {
    guard let root = try JSONSerialization.jsonObject(with: data) as? [String: Any],
      let features = root["features"] as? [[String: Any]]
    else { throw FeedError.malformed }
    var records: [GeoRecord] = []
    var seen = Set<String>()
    for (index, feature) in features.enumerated() {
      guard let geometry = feature["geometry"] as? [String: Any],
        geometry["type"] as? String == "Point", let coordinates = geometry["coordinates"] as? [Any],
        coordinates.count >= 2, let lon = jsonNumber(coordinates[0]),
        let lat = jsonNumber(coordinates[1]),
        let coordinate = Coordinate(latitude: lat, longitude: lon),
        let properties = feature["properties"] as? [String: Any]
      else { throw FeedError.malformed }
      let magnitude = jsonNumber(properties["mag"])
      if let value = properties["mag"], !(value is NSNull), magnitude == nil {
        throw FeedError.malformed
      }
      if let magnitude, magnitude > 10 { throw FeedError.malformed }
      guard let magnitude, magnitude >= 2.5 else { continue }
      let id = String(describing: feature["id"] ?? "event-\(index + 1)")
      guard seen.insert(id).inserted else { throw FeedError.malformed }
      let time = jsonNumber(properties["time"]).map { Date(timeIntervalSince1970: $0 / 1_000) }
      let place = properties["place"] as? String ?? "Location name unavailable"
      var details = ["Magnitude": String(format: "%.1f", magnitude), "Location": place]
      if coordinates.count > 2 {
        if let depth = jsonNumber(coordinates[2]) {
          details["Depth"] = String(format: "%.1f km", depth)
        } else if !(coordinates[2] is NSNull) {
          throw FeedError.malformed
        }
      }
      if let link = properties["url"] as? String, URL(string: link)?.scheme == "https" {
        details["Source URL"] = link
      }
      records.append(
        .init(
          id: "usgs:\(id)", feed: .earthquakes,
          label: "M\(String(format: "%.1f", magnitude)) · \(place)", coordinate: coordinate,
          altitude: nil, observedAt: time, source: "USGS", details: details))
    }
    return Snapshot(records: records, rejected: 0, source: "USGS")
  }
  public static func adsb(_ data: Data, feed: FeedKind) throws -> Snapshot {
    guard let root = try JSONSerialization.jsonObject(with: data) as? [String: Any],
      let aircraft = root["ac"] as? [[String: Any]]
    else { throw FeedError.malformed }
    let epoch = number(root["now"]).map { $0 > 10_000_000_000 ? $0 / 1_000 : $0 }
    var records: [GeoRecord] = []
    var rejected = 0
    var seen = Set<String>()
    for row in aircraft {
      guard let hex = row["hex"] as? String, !hex.isEmpty, let lat = number(row["lat"]),
        let lon = number(row["lon"]), let coordinate = Coordinate(latitude: lat, longitude: lon),
        seen.insert(hex.lowercased()).inserted
      else {
        rejected += 1
        continue
      }
      let label = (row["flight"] as? String ?? row["r"] as? String ?? hex).trimmingCharacters(
        in: .whitespacesAndNewlines)
      let ground = row["alt_baro"] as? String == "ground"
      let altitude = number(row["alt_geom"]).map { $0 * 0.3048 }
      let observed = epoch.flatMap { timestamp in
        number(row["seen_pos"]).map { Date(timeIntervalSince1970: timestamp - max(0, $0)) }
      }
      var details = [
        "ICAO": hex.uppercased(), "Type": row["t"] as? String ?? "Unknown",
        "Registration": row["r"] as? String ?? "Unknown",
        "Ground state": ground ? "Reported on ground" : "Not reported on ground",
      ]
      if let speed = number(row["gs"]) {
        details["Ground speed"] = String(format: "%.0f kt", speed)
      }
      if let alt = number(row["alt_baro"]) {
        details["Barometric altitude"] = String(format: "%.0f ft", alt)
      }
      records.append(
        .init(
          id: "\(feed.rawValue):\(hex.lowercased())", feed: feed,
          label: label.isEmpty ? hex.uppercased() : label, coordinate: coordinate,
          altitude: altitude, observedAt: observed, source: "adsb.lol", details: details))
    }
    return Snapshot(records: records, rejected: rejected, source: "adsb.lol")
  }
  public static func openSky(_ data: Data) throws -> Snapshot {
    guard let root = try JSONSerialization.jsonObject(with: data) as? [String: Any] else {
      throw FeedError.malformed
    }
    if root["states"] is NSNull {
      return Snapshot(records: [], rejected: 0, source: "OpenSky Network")
    }
    guard let states = root["states"] as? [[Any]] else { throw FeedError.malformed }
    var records: [GeoRecord] = []
    var rejected = 0
    var seen = Set<String>()
    for row in states {
      guard row.count >= 14, let hex = row[0] as? String, !hex.isEmpty, let lon = number(row[5]),
        let lat = number(row[6]), let coordinate = Coordinate(latitude: lat, longitude: lon),
        seen.insert(hex).inserted
      else {
        rejected += 1
        continue
      }
      let label = (row[1] as? String ?? hex).trimmingCharacters(in: .whitespacesAndNewlines)
      var details = ["ICAO": hex.uppercased(), "Origin country": row[2] as? String ?? "Unknown"]
      if let speed = number(row[9]) {
        details["Ground speed"] = String(format: "%.0f kt", speed / 0.514444)
      }
      if let alt = number(row[7]) {
        details["Barometric altitude"] = String(format: "%.0f ft", alt / 0.3048)
      }
      records.append(
        .init(
          id: "flights:\(hex)", feed: .flights, label: label.isEmpty ? hex.uppercased() : label,
          coordinate: coordinate, altitude: number(row[13]),
          observedAt: number(row[3]).map { Date(timeIntervalSince1970: $0) },
          source: "OpenSky Network", details: details))
    }
    return Snapshot(records: records, rejected: rejected, source: "OpenSky Network")
  }
}
