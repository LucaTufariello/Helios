import Foundation

public enum MissionReviewState: String, Codable, CaseIterable, Sendable {
  case unreviewed = "Unreviewed"
  case corroborated = "Corroborated"
  case uncertain = "Uncertain"
  case dismissed = "Dismissed"
}
/// Analyst-curated links within one mission area; source files remain in their libraries.
public struct MissionDossier: Codable, Equatable, Sendable {
  public var version = 1
  public var linkedEvidenceIDs: [UUID] = []
  public var baselineEvidenceIDs: [UUID] = []
  public var sourceSiteEvidenceID: UUID?
  public var monitoring: MissionMonitoring?
  public init() {}
  public func isValid(
    evidence: Set<UUID>, rasters: Set<UUID>? = nil, sites: Set<UUID>? = nil,
    monitors: Set<UUID>? = nil, radarBands: [UUID: String]? = nil
  ) -> Bool {
    let linked = Set(linkedEvidenceIDs)
    let baselines = Set(baselineEvidenceIDs)
    return version == 1 && linkedEvidenceIDs.count <= 500 && baselineEvidenceIDs.count <= 500
      && linked.count == linkedEvidenceIDs.count && baselines.count == baselineEvidenceIDs.count
      && linked.isSubset(of: evidence) && baselines.isSubset(of: linked)
      && (rasters.map { baselines.isSubset(of: $0) } ?? true)
      && (sourceSiteEvidenceID.map { linked.contains($0) && (sites?.contains($0) ?? true) } ?? true)
      && (monitoring?.isValid(pins: baselines, monitors: monitors, radarBands: radarBands) ?? true)
  }
  public mutating func removeEvidence(_ id: UUID) {
    linkedEvidenceIDs.removeAll { $0 == id }
    baselineEvidenceIDs.removeAll { $0 == id }
    if sourceSiteEvidenceID == id { sourceSiteEvidenceID = nil }
    if monitoring?.baselineEvidenceID == id {
      monitoring?.baselineEvidenceID = nil
      monitoring?.enabled = false
    }
  }
}
public struct MissionArea: Codable, Identifiable, Equatable, Sendable {
  public var id = UUID()
  public var name: String
  public var purpose: String
  public var region: SentinelRegion
  public var dossier: MissionDossier?
  public init(name: String, purpose: String = "", region: SentinelRegion) {
    self.name = name
    self.purpose = purpose
    self.region = region
  }
  public var isValid: Bool {
    !name.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty && name.count <= 160
      && purpose.count <= 2000
  }
}
public struct MissionObservation: Codable, Identifiable, Equatable, Sendable {
  public var id = UUID()
  public var title: String
  public var category: String
  public var coordinate: Coordinate
  public var bounds: [Double]?
  public var evidenceID: UUID?
  public var areaID: UUID?
  public var note = ""
  public var state = MissionReviewState.unreviewed
  public var method: String
  public var modelScore: Double?
  public var modelName: String?
  public var inputSHA256: String?
  public var inputTransform: String?
  public var modelSHA256: String?
  public var modelContractSHA256: String?
  public var modelBackend: String?
  /// ONNX proposals use normalized upper-left XYXY preview coordinates.
  public var imageBox: [Double]?
  /// The corresponding upper-left XYXY coordinates in the retained PNG preview.
  public var imagePixelBox: [Double]?
  public var observedAt: Date?
  public var createdAt = Date()
  public init(
    title: String, category: String, coordinate: Coordinate,
    method: String = "Analyst identification"
  ) {
    self.title = title
    self.category = category
    self.coordinate = coordinate
    self.method = method
  }
  public var isValid: Bool {
    !title.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty && title.count <= 240
      && !category.isEmpty && category.count <= 120 && note.count <= 4000
      && !method.isEmpty && method.count <= 500
      && (bounds.map(ChangeReviewResult.validBounds) ?? true)
      && (modelScore.map { $0.isFinite && (0...1).contains($0) } ?? true)
      && (modelName.map { !$0.isEmpty && $0.count <= 200 } ?? true)
      && (inputTransform.map { $0.count <= 200 } ?? true)
      && (inputSHA256.map(Self.validSHA256) ?? true)
      && validModelProvenance
      && (observedAt?.timeIntervalSince1970.isFinite ?? true)
      && createdAt.timeIntervalSince1970.isFinite
  }
  private static func validSHA256(_ value: String) -> Bool {
    value.count == 64
      && value.utf8.allSatisfy { (48...57).contains($0) || (97...102).contains($0) }
  }
  private static func validImageBox(_ box: [Double], maximum: Double) -> Bool {
    box.count == 4 && box.allSatisfy { $0.isFinite && (0...maximum).contains($0) }
      && box[0] < box[2] && box[1] < box[3]
  }
  private var validModelProvenance: Bool {
    // These fields are absent from legacy manual and Core ML observations.
    if modelSHA256 == nil && modelContractSHA256 == nil && modelBackend == nil
      && imageBox == nil && imagePixelBox == nil
    {
      return true
    }
    guard let modelSHA256, let modelContractSHA256, let modelBackend, let imageBox,
      let imagePixelBox
    else { return false }
    return Self.validSHA256(modelSHA256) && Self.validSHA256(modelContractSHA256)
      && ["onnx-cpu", "onnx-tinyyolov2-voc-cpu"].contains(modelBackend)
      && Self.validImageBox(imageBox, maximum: 1)
      && Self.validImageBox(imagePixelBox, maximum: 2048)
  }
}
public struct MissionNote: Codable, Identifiable, Equatable, Sendable {
  public var id = UUID()
  public var title: String
  public var body: String
  public var reference = ""
  public var evidenceID: UUID?
  public var areaID: UUID?
  public var createdAt = Date()
  public init(title: String, body: String) {
    self.title = title
    self.body = body
  }
  public var isValid: Bool {
    !title.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty && title.count <= 240
      && body.count <= 8000 && reference.count <= 2000 && createdAt.timeIntervalSince1970.isFinite
  }
}
public struct MissionTask: Codable, Identifiable, Equatable, Sendable {
  public var id = UUID()
  public var title: String
  public var due: Date?
  public var done = false
  public var areaID: UUID?
  public var observationID: UUID?
  public var monitoringEventID: UUID?
  public var findingID: UUID?
  public var owner: String?
  public var dependencyIDs: [UUID]?
  public init(title: String, due: Date? = nil) {
    self.title = title
    self.due = due
  }
  public var isValid: Bool {
    !title.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty && title.count <= 240
      && (owner?.count ?? 0) <= 160
      && (due?.timeIntervalSince1970.isFinite ?? true)
  }
}
public struct MissionActivity: Codable, Identifiable, Equatable, Sendable {
  public var id = UUID()
  public var time = Date()
  public var message: String
  public init(_ message: String) { self.message = message }
}
public enum MissionMonitorKind: String, Codable, CaseIterable, Sendable {
  case sentinel2 = "New Sentinel-2 imagery"
  case sentinel1 = "New Sentinel-1 acquisitions"
  case evidence = "Evidence availability"
}
public struct MissionMonitor: Codable, Identifiable, Equatable, Sendable {
  public var id = UUID()
  public var kind: MissionMonitorKind
  public var areaID: UUID?
  public var enabled = false
  public var intervalHours = 24
  public var lookbackDays = 14
  public var maximumCloud = 30.0
  public var lastAttempt: Date?
  public var lastSuccess: Date?
  public var seenIDs: [String] = []
  public var summary = "Not run"
  public var failure: String?
  public init(kind: MissionMonitorKind, areaID: UUID? = nil) {
    self.kind = kind
    self.areaID = areaID
  }
  public func isDue(at now: Date) -> Bool {
    enabled
      && (lastAttempt.map { now.timeIntervalSince($0) >= Double(intervalHours) * 3600 } ?? true)
  }
  public var isValid: Bool {
    (1...168).contains(intervalHours) && (1...90).contains(lookbackDays)
      && maximumCloud.isFinite && (0...100).contains(maximumCloud)
      && seenIDs.count <= 2000 && seenIDs.allSatisfy { !$0.isEmpty && $0.count <= 200 }
      && summary.count <= 2000 && (failure?.count ?? 0) <= 2000
      && (lastAttempt?.timeIntervalSince1970.isFinite ?? true)
      && (lastSuccess?.timeIntervalSince1970.isFinite ?? true)
      && (kind == .evidence || areaID != nil)
  }
  /// First successful query establishes a baseline. Failures must not advance this checkpoint.
  public mutating func receive(ids: [String], at date: Date, limited: Bool) -> Int {
    let known = Set(seenIDs)
    let incoming = Array(Set(ids)).sorted()
    let newCount = lastSuccess == nil ? 0 : incoming.filter { !known.contains($0) }.count
    summary =
      lastSuccess == nil
      ? "Baseline: \(incoming.count) acquisitions"
      : "\(newCount) newly seen acquisitions · \(incoming.count) returned"
    if limited { summary += " · catalogue result limit reached; coverage may be incomplete" }
    // New IDs first, bounded history. The retained lookback is explicit in the UI.
    seenIDs = Array((incoming + seenIDs.filter { !incoming.contains($0) }).prefix(2000))
    lastAttempt = date
    lastSuccess = date
    failure = nil
    return newCount
  }
}
/// Optional versioned extension leaves existing mission documents readable.
public struct MissionBrief: Codable, Equatable, Sendable {
  public var version = 1
  public var areas: [MissionArea] = []
  public var observations: [MissionObservation] = []
  public var notes: [MissionNote] = []
  public var tasks: [MissionTask] = []
  public var monitors: [MissionMonitor] = []
  public var activity: [MissionActivity] = []
  public var monitoringInbox: [MissionMonitoringEvent]?
  public var investigation: MissionInvestigation?
  public init() {}
  public mutating func record(_ message: String) {
    activity.insert(MissionActivity(message), at: 0)
    activity = Array(activity.prefix(200))
  }
  public mutating func removeDossierEvidence(_ id: UUID) {
    for i in areas.indices { areas[i].dossier?.removeEvidence(id) }
    if var inbox = monitoringInbox {
      for i in inbox.indices where inbox[i].baselineEvidenceID == id {
        inbox[i].baselineEvidenceID = nil
        inbox[i].status = "Stale"
        inbox[i].reason = "Referenced evidence removed; source job retained"
        inbox[i].updatedAt = Date()
      }
      monitoringInbox = inbox
    }
    for i in monitors.indices {
      if areas.contains(where: {
        $0.dossier?.monitoring?.discoveryMonitorID == monitors[i].id
          && $0.dossier?.monitoring?.enabled == false
      }) {
        monitors[i].enabled = false
      }
    }
  }
  public mutating func removeMonitoringArea(_ id: UUID) {
    if var inbox = monitoringInbox {
      for i in inbox.indices where inbox[i].areaID == id {
        inbox[i].areaID = nil
        inbox[i].status = "Stale"
        inbox[i].reason = "Area removed; source job retained"
        inbox[i].updatedAt = Date()
      }
      monitoringInbox = inbox
    }
  }
  public mutating func removeMonitoringCheck(_ id: UUID) {
    for i in areas.indices where areas[i].dossier?.monitoring?.discoveryMonitorID == id {
      areas[i].dossier?.monitoring?.discoveryMonitorID = nil
      areas[i].dossier?.monitoring?.enabled = false
    }
  }
  public mutating func removeObservationReferences(_ id: UUID) {
    for i in tasks.indices where tasks[i].observationID == id { tasks[i].observationID = nil }
  }
  public func isValid(
    evidence: Set<UUID>, rasterEvidence: Set<UUID>? = nil, siteEvidence: Set<UUID>? = nil,
    radarBands: [UUID: String]? = nil
  ) -> Bool {
    let areaIDs = Set(areas.map(\.id))
    let observationIDs = Set(observations.map(\.id))
    let inbox = monitoringInbox ?? []
    let inboxIDs = Set(inbox.map(\.id))
    func linked(_ id: UUID?) -> Bool { id.map { areaIDs.contains($0) } ?? true }
    func source(_ id: UUID?) -> Bool { id.map { evidence.contains($0) } ?? true }
    return validInvestigationTaskDependencies(tasks)
      && (investigation?.isValid(tasks: tasks, inbox: inbox)
        ?? tasks.allSatisfy { $0.findingID == nil })
      && inbox.count <= 200 && inboxIDs.count == inbox.count
      && Set(inbox.map(\.key)).count == inbox.count
      && inbox.allSatisfy { $0.isValid(areas: areaIDs, evidence: evidence) }
      && version == 1 && areas.count <= 100 && observations.count <= 500 && notes.count <= 200
      && tasks.count <= 200 && monitors.count <= 50 && activity.count <= 200
      && areaIDs.count == areas.count && Set(observations.map(\.id)).count == observations.count
      && Set(notes.map(\.id)).count == notes.count && Set(tasks.map(\.id)).count == tasks.count
      && Set(monitors.map(\.id)).count == monitors.count
      && Set(activity.map(\.id)).count == activity.count
      && areas.allSatisfy { area in
        area.isValid
          && (area.dossier?.monitoring?.discoveryMonitorID.map { monitorID in
            monitors.contains {
              $0.id == monitorID && $0.areaID == area.id && $0.kind == .sentinel1
            }
          } ?? true)
          && (area.dossier?.isValid(
            evidence: evidence, rasters: rasterEvidence, sites: siteEvidence,
            monitors: Set(monitors.map(\.id)), radarBands: radarBands) ?? true)
      }
      && observations.allSatisfy { $0.isValid && linked($0.areaID) && source($0.evidenceID) }
      && notes.allSatisfy { $0.isValid && linked($0.areaID) && source($0.evidenceID) }
      && tasks.allSatisfy {
        $0.isValid && linked($0.areaID)
          && ($0.observationID.map { observationIDs.contains($0) } ?? true)
          && ($0.monitoringEventID.map { inboxIDs.contains($0) } ?? true)
      }
      && monitors.allSatisfy { $0.isValid && linked($0.areaID) }
      && activity.allSatisfy { $0.message.count <= 2000 && $0.time.timeIntervalSince1970.isFinite }
  }
}
public enum MissionImageGeometry {
  /// Vision boxes use normalized lower-left coordinates. Preview bounds are EPSG:4326 outer edges.
  public static func bounds(x: Double, y: Double, width: Double, height: Double, image: [Double])
    -> [Double]?
  {
    guard ChangeReviewResult.validBounds(image), [x, y, width, height].allSatisfy(\.isFinite),
      x >= 0, y >= 0, width > 0, height > 0, x + width <= 1.000001, y + height <= 1.000001
    else { return nil }
    return [
      image[0] + x * (image[2] - image[0]), image[1] + y * (image[3] - image[1]),
      image[0] + min(1, x + width) * (image[2] - image[0]),
      image[1] + min(1, y + height) * (image[3] - image[1]),
    ]
  }
}
