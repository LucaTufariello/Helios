import Foundation

public enum Basemap: String, CaseIterable, Codable, Sendable {
  case satellite, streets
  public var name: String { self == .satellite ? "Esri Satellite" : "OpenStreetMap" }
  public var credit: String {
    self == .satellite
      ? "Esri and imagery providers · dated mosaic" : "© OpenStreetMap contributors · ODbL"
  }
  public func url(_ tile: TileID) -> URL {
    self == .satellite
      ? tile.url
      : URL(string: "https://tile.openstreetmap.org/\(tile.zoom)/\(tile.x)/\(tile.y).png")!
  }
}
public enum DisplayStyle: String, CaseIterable, Codable, Sendable {
  case standard, monochrome, nightVision, thermal
  public var name: String {
    switch self {
    case .standard: "Standard"
    case .monochrome: "Monochrome"
    case .nightVision: "Night vision shader"
    case .thermal: "Thermal shader"
    }
  }
}
public struct LocalAnnotation: Codable, Identifiable, Hashable, Sendable {
  public var id = UUID()
  public var title: String
  public var points: [Coordinate]
  public var closed: Bool
  public var createdAt = Date()
  public init(title: String, points: [Coordinate], closed: Bool = false) {
    self.title = title
    self.points = points
    self.closed = closed
  }
  public var isValid: Bool {
    !title.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty && title.count <= 256
      && !points.isEmpty && points.count <= 2000 && (!closed || points.count >= 3)
      && createdAt.timeIntervalSince1970.isFinite
  }
  public var record: GeoRecord? {
    guard isValid else { return nil }
    let path = closed ? points + [points[0]] : points
    return GeoRecord(
      id: "annotation:\(id.uuidString)", feed: .annotations, label: title, coordinate: points[0],
      altitude: nil, observedAt: createdAt, source: "User annotation",
      details: [
        "Evidence": "User-authored note or drawing; no provider measurement is implied",
        "Vertices": "\(points.count)",
      ], footprint: closed ? path : [], paths: path.count > 1 ? [path] : [])
  }
}
public struct WorkspaceState: Codable, Sendable {
  public var satelliteGroup = SatelliteGroup.stations
  public var transitOperator = "mbta"
  public var cameraProvider = CameraProvider.london
  public var sentinelCollection = "SENTINEL-2"
  public var version = 1
  public var camera = Camera.initial
  public var layers: Set<FeedKind> = []
  public var basemap = Basemap.satellite
  public var style = DisplayStyle.standard
  public var imagery = DailyImagery.off
  public var opacity = 0.85
  public var solar = false
  public var terrain = true
  public var trackedID: String?
  public var annotations: [LocalAnnotation] = []
  public init() {}
  private enum CodingKeys: String, CodingKey {
    case satelliteGroup, transitOperator, cameraProvider, sentinelCollection, version, camera,
      layers, basemap, style, imagery, opacity, solar, terrain, trackedID, annotations
  }
  public init(from decoder: Decoder) throws {
    self.init()
    let values = try decoder.container(keyedBy: CodingKeys.self)
    satelliteGroup = try values.decode(SatelliteGroup.self, forKey: .satelliteGroup)
    transitOperator = try values.decode(String.self, forKey: .transitOperator)
    cameraProvider = try values.decode(CameraProvider.self, forKey: .cameraProvider)
    sentinelCollection = try values.decode(String.self, forKey: .sentinelCollection)
    version = try values.decode(Int.self, forKey: .version)
    camera = try values.decode(Camera.self, forKey: .camera)
    for raw in try values.decode(Set<String>.self, forKey: .layers) {
      // Keep previously exported workspaces usable after removing this source.
      if raw == "neighborhoods" { continue }
      guard let feed = FeedKind(rawValue: raw) else {
        throw DecodingError.dataCorruptedError(
          forKey: .layers, in: values, debugDescription: "Unknown data source: \(raw)")
      }
      layers.insert(feed)
    }
    basemap = try values.decode(Basemap.self, forKey: .basemap)
    style = try values.decode(DisplayStyle.self, forKey: .style)
    imagery = try values.decode(DailyImagery.self, forKey: .imagery)
    opacity = try values.decode(Double.self, forKey: .opacity)
    solar = try values.decode(Bool.self, forKey: .solar)
    terrain = try values.decodeIfPresent(Bool.self, forKey: .terrain) ?? true
    trackedID = try values.decodeIfPresent(String.self, forKey: .trackedID)
    if trackedID?.hasPrefix("neighborhoods:") == true { trackedID = nil }
    annotations = try values.decode([LocalAnnotation].self, forKey: .annotations)
  }
  public func validated() throws -> WorkspaceState {
    guard version == 1, TransitOperator.catalog.contains(where: { $0.id == transitOperator }),
      ["SENTINEL-1", "SENTINEL-2"].contains(sentinelCollection), opacity.isFinite,
      (0...1).contains(opacity), annotations.count <= 500,
      annotations.allSatisfy(\.isValid), Set(annotations.map(\.id)).count == annotations.count,
      (trackedID?.count ?? 0) <= 200
    else { throw FeedError.malformed }
    return self
  }
  public func shareURL() throws -> URL {
    // Annotations and credentials are deliberately not shared without an explicit export.
    var view = self
    view.annotations = []
    let data = try JSONEncoder().encode(view)
    guard data.count < 10_000 else { throw FeedError.oversized }
    var url = URLComponents()
    url.scheme = "helios"
    url.host = "workspace"
    url.queryItems = [.init(name: "state", value: data.base64EncodedString())]
    return url.url!
  }
  public static func from(_ url: URL) -> WorkspaceState? {
    if let camera = Camera.fromShareURL(url) {
      var state = WorkspaceState()
      state.camera = camera
      return state
    }
    guard url.absoluteString.count <= 16_000, url.scheme == "helios", url.host == "workspace",
      let items = URLComponents(url: url, resolvingAgainstBaseURL: false)?.queryItems,
      items.count == 1, items[0].name == "state", let raw = items[0].value,
      let data = Data(base64Encoded: raw),
      let state = try? JSONDecoder().decode(WorkspaceState.self, from: data),
      state.annotations.isEmpty
    else { return nil }
    return try? state.validated()
  }
}
public struct TrackFix: Hashable, Sendable {
  public let coordinate: Coordinate
  public let altitude: Double?
  public let time: Date
}
public struct TrackHistory: Sendable {
  private var tracks: [String: [TrackFix]] = [:]
  public init() {}
  public mutating func ingest(_ records: [GeoRecord], now: Date = Date()) {
    for record in records
    where [.flights, .military, .vessels, .transit, .satellites].contains(record.feed) {
      // Propagated orbits are estimates at the render time, not historical measurements.
      guard record.feed != .satellites, let time = record.observedAt,
        time <= now.addingTimeInterval(120), time >= now.addingTimeInterval(-3600)
      else { continue }
      let fix = TrackFix(coordinate: record.coordinate, altitude: record.altitude, time: time)
      var history = tracks[record.id, default: []]
      guard history.last?.time ?? .distantPast < time else { continue }
      history.append(fix)
      tracks[record.id] = Array(
        history.filter { now.timeIntervalSince($0.time) <= 3600 }.suffix(240))
    }
    tracks = tracks.filter { $0.value.last.map { now.timeIntervalSince($0.time) <= 3600 } ?? false }
    while tracks.count > 5000,
      let id = tracks.min(by: {
        ($0.value.last?.time ?? .distantPast) < ($1.value.last?.time ?? .distantPast)
      })?.key
    { tracks.removeValue(forKey: id) }
  }
  public func fixes(for id: String?) -> [TrackFix] { id.flatMap { tracks[$0] } ?? [] }
  public func delayed(_ record: GeoRecord, at time: Date) -> GeoRecord? {
    guard let fix = fixes(for: record.id).last(where: { $0.time <= time }) else { return nil }
    var details = record.details
    details["Playback"] = "Retained observed fix; no interpolation or extrapolation"
    return GeoRecord(
      id: record.id, feed: record.feed, label: record.label, coordinate: fix.coordinate,
      altitude: fix.altitude, observedAt: fix.time, source: record.source, details: details)
  }
}
public struct ScenePath: Hashable, Sendable {
  public var points: [Coordinate]
  public var altitudes: [Double]
  public var kind: Kind
  public enum Kind: Hashable, Sendable {
    case reference, route, annotation, trail, orbit, trafficFree, trafficSlow, trafficJam
  }
  public init(points: [Coordinate], altitudes: [Double] = [], kind: Kind) {
    self.points = points
    self.altitudes = altitudes
    self.kind = kind
  }
}
