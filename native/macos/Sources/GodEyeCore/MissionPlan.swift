import Foundation

/// Analyst-authored investigation plans, not autonomous tasking or operational commands.
public enum MissionItemKind: String, Codable, CaseIterable, Sendable {
  case view, site, record, radar, optical, annotation
}
public struct MissionRecord: Codable, Equatable, Sendable {
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
  public init(_ record: GeoRecord) {
    id = record.id
    feed = record.feed
    label = record.label
    coordinate = record.coordinate
    altitude = record.altitude
    observedAt = record.observedAt
    source = record.source
    // Media delivery links are transient and may contain access parameters.
    details = record.details.filter { !["Frame URL", "Video URL"].contains($0.key) }
    footprint = record.footprint
    paths = record.paths
  }
  public var isValid: Bool {
    !id.isEmpty && id.count <= 512 && !label.isEmpty && label.count <= 1024
      && source.count <= 1024 && details.count <= 100
      && details.allSatisfy { $0.key.count <= 256 && $0.value.count <= 8192 }
      && footprint.count <= 2000 && paths.count <= 64
      && paths.allSatisfy { $0.count <= 2000 } && paths.reduce(0, { $0 + $1.count }) <= 20_000
      && (altitude?.isFinite ?? true) && (observedAt?.timeIntervalSince1970.isFinite ?? true)
  }
}
public struct MissionItem: Codable, Identifiable, Equatable, Sendable {
  public var id = UUID()
  public var kind: MissionItemKind
  public var referenceID: String
  public var title: String
  public var savedAt = Date()
  public var camera: Camera?
  public var site: InfrastructureSite?
  public var record: MissionRecord?
  public var annotation: LocalAnnotation?
  public init(kind: MissionItemKind, referenceID: String, title: String) {
    self.kind = kind
    self.referenceID = referenceID
    self.title = title
  }
  public var isValid: Bool {
    guard !title.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty, title.count <= 1024,
      !referenceID.isEmpty, referenceID.count <= 512, savedAt.timeIntervalSince1970.isFinite
    else { return false }
    switch kind {
    case .view: return camera != nil && site == nil && record == nil && annotation == nil
    case .site: return site?.isValid == true && referenceID == site?.id.uuidString
    case .record: return record?.isValid == true && referenceID == record?.id
    case .annotation: return annotation?.isValid == true && referenceID == annotation?.id.uuidString
    case .radar, .optical:
      return UUID(uuidString: referenceID) != nil && camera == nil && site == nil && record == nil
        && annotation == nil
    }
  }
}
/// A saved visual comparison configuration; it never implies scientific registration.
public struct MissionComparison: Codable, Equatable, Sendable {
  public var baseID: String
  public var topID: String
  public var blend: Double
  public init(baseID: String, topID: String, blend: Double) {
    self.baseID = baseID
    self.topID = topID
    self.blend = blend
  }
  private func validReference(_ id: String) -> Bool {
    if id.isEmpty { return true }
    let parts = id.split(separator: ":", omittingEmptySubsequences: false)
    guard parts.count >= 2, UUID(uuidString: String(parts[1])) != nil else { return false }
    if parts[0] == "optical" { return parts.count == 2 }
    return parts[0] == "radar" && parts.count == 3
      && ["VV", "VH", "Composite"].contains(String(parts[2]))
  }
  public var isValid: Bool {
    validReference(baseID) && validReference(topID) && blend.isFinite && (0...1).contains(blend)
  }
}
public struct MissionPlan: Codable, Identifiable, Equatable, Sendable {
  public var id = UUID()
  public var name: String
  public var objective = ""
  public var notes = ""
  public var createdAt = Date()
  public var updatedAt = Date()
  public var items: [MissionItem] = []
  public var comparison: MissionComparison?
  public init(name: String) { self.name = name.trimmingCharacters(in: .whitespacesAndNewlines) }
  public var isValid: Bool {
    !name.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty && name.count <= 120
      && (comparison?.isValid ?? true) && objective.count <= 4000 && notes.count <= 32_000
      && items.count <= 500
      && items.allSatisfy(\.isValid) && Set(items.map(\.id)).count == items.count
      && createdAt.timeIntervalSince1970.isFinite && updatedAt.timeIntervalSince1970.isFinite
  }
}
public enum MissionDocuments {
  public static func load(from url: URL) throws -> [MissionPlan] {
    guard FileManager.default.fileExists(atPath: url.path) else { return [] }
    let plans = try SarProcessor.read([MissionPlan].self, from: url)
    guard plans.count <= 200, plans.allSatisfy(\.isValid), Set(plans.map(\.id)).count == plans.count
    else { throw FeedError.malformed }
    return plans
  }
  public static func save(_ plans: [MissionPlan], to url: URL) throws {
    guard plans.count <= 200, plans.allSatisfy(\.isValid), Set(plans.map(\.id)).count == plans.count
    else { throw FeedError.malformed }
    _ = try load(from: url)  // Never overwrite an unreadable existing library.
    // Numeric Foundation dates preserve subsecond observation times in record snapshots.
    try SatelliteDocuments.save(plans, to: url, dateEncodingStrategy: .deferredToDate)
  }
}

public enum RasterComparison {
  public static func overlaps(_ a: [Double], _ b: [Double]) -> Bool {
    ChangeReviewResult.validBounds(a) && ChangeReviewResult.validBounds(b)
      && max(a[0], b[0]) < min(a[2], b[2]) && max(a[1], b[1]) < min(a[3], b[3])
  }
}
