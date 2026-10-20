import Foundation
import XCTest

@testable import GodEyeCore

final class MissionOperationsTests: XCTestCase {
  private func region() throws -> SentinelRegion {
    try SentinelRegion(west: 8, south: 44, east: 8.1, north: 44.1)
  }
  private func file() throws -> URL {
    let root = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
    try FileManager.default.createDirectory(at: root, withIntermediateDirectories: true)
    addTeardownBlock { try? FileManager.default.removeItem(at: root) }
    return root.appendingPathComponent("plans.json")
  }
  private func onnxObservation() -> MissionObservation {
    var item = MissionObservation(
      title: "Generated proposal", category: "Building",
      coordinate: Coordinate(latitude: 44.01, longitude: 8.01)!,
      method: "ONNX CPU proposal · GENERATED-QA-detector.onnx")
    item.modelScore = 0.81
    item.modelName = "GENERATED-QA-detector.onnx"
    item.inputSHA256 = String(repeating: "a", count: 64)
    item.inputTransform = "RGB; normalized upper-left XYXY"
    item.modelSHA256 = String(repeating: "b", count: 64)
    item.modelContractSHA256 = String(repeating: "c", count: 64)
    item.modelBackend = "onnx-cpu"
    item.imageBox = [0.1, 0.2, 0.6, 0.7]
    item.imagePixelBox = [20, 40, 120, 140]
    return item
  }
  func testLegacyMissionRemainsReadableWithoutBrief() throws {
    let file = try file()
    let old = MissionPlan(name: "Legacy")
    try MissionDocuments.save([old], to: file)
    let data = try Data(contentsOf: file)
    XCTAssertFalse(String(decoding: data, as: UTF8.self).contains("brief"))
    XCTAssertEqual(try MissionDocuments.load(from: file), [old])
  }
  func testLegacyBackupIsCreatedOnceAndPreserved() throws {
    let file = try file()
    var mission = MissionPlan(name: "Legacy")
    try MissionDocuments.save([mission], to: file)
    let before = try Data(contentsOf: file)
    mission.brief = MissionBrief()
    try MissionDocuments.save([mission], to: file)
    let backup = file.deletingLastPathComponent().appendingPathComponent(
      "plans-before-mission-control.json")
    XCTAssertEqual(try Data(contentsOf: backup), before)
    mission.brief?.notes.append(MissionNote(title: "Review", body: "Evidence"))
    try MissionDocuments.save([mission], to: file)
    XCTAssertEqual(try Data(contentsOf: backup), before)
  }
  func testRichMissionExceedsLegacyCapWithoutRelaxingOtherDocuments() throws {
    let file = try file()
    var mission = MissionPlan(name: "Rich review")
    mission.brief = MissionBrief()
    mission.brief?.notes = (0..<150).map {
      MissionNote(title: "Note \($0)", body: String(repeating: "a", count: 8000))
    }
    try MissionDocuments.save([mission], to: file)
    XCTAssertGreaterThan(try Data(contentsOf: file).count, 1024 * 1024)
    XCTAssertEqual(try MissionDocuments.load(from: file), [mission])
    XCTAssertThrowsError(try SarProcessor.read([MissionPlan].self, from: file))
  }
  func testCompleteMissionRoundTripWithLinkedProvenance() throws {
    var mission = MissionPlan(name: "Infrastructure assessment")
    let evidence = MissionItem(kind: .optical, referenceID: UUID().uuidString, title: "Acquisition")
    mission.items = [evidence]
    var brief = MissionBrief()
    let area = MissionArea(
      name: "Port", purpose: "Assess infrastructure changes", region: try region())
    brief.areas = [area]
    var observation = MissionObservation(
      title: "Warehouse", category: "Building",
      coordinate: Coordinate(latitude: 44.01, longitude: 8.01)!)
    observation.evidenceID = evidence.id
    observation.areaID = area.id
    observation.observedAt = Date(timeIntervalSince1970: 1000.123456)
    observation.modelScore = 0.81
    observation.modelName = "GENERATED-QA-detector.mlmodel"
    observation.inputSHA256 = String(repeating: "a", count: 64)
    observation.inputTransform = "Vision scaleFill"
    observation.state = .uncertain
    XCTAssertNil(observation.modelSHA256)
    XCTAssertNil(observation.modelContractSHA256)
    XCTAssertNil(observation.modelBackend)
    XCTAssertNil(observation.imageBox)
    XCTAssertNil(observation.imagePixelBox)
    brief.observations = [observation]
    var note = MissionNote(title: "Cross-check", body: "Compare with another dated acquisition.")
    note.areaID = area.id
    note.evidenceID = evidence.id
    brief.notes = [note]
    brief.tasks = [MissionTask(title: "Review new acquisition", due: Date())]
    brief.monitors = [MissionMonitor(kind: .sentinel2, areaID: area.id)]
    brief.record("Review created")
    mission.brief = brief
    XCTAssertTrue(mission.isValid)
    let file = try file()
    try MissionDocuments.save([mission], to: file)
    XCTAssertEqual(try MissionDocuments.load(from: file), [mission])
  }
  func testONNXModelProvenanceHashesAndBackendsAreStrict() {
    var item = onnxObservation()
    XCTAssertTrue(item.isValid)
    item.modelBackend = "onnx-tinyyolov2-voc-cpu"
    XCTAssertTrue(item.isValid)
    for invalid in ["", "coreml", "onnx-gpu", "onnx-cpu "] {
      item.modelBackend = invalid
      XCTAssertFalse(item.isValid, invalid)
    }
    for field in [
      \MissionObservation.modelSHA256, \MissionObservation.modelContractSHA256,
    ] {
      for invalid in [
        "", String(repeating: "b", count: 63), String(repeating: "B", count: 64),
        String(repeating: "g", count: 64),
      ] {
        item = onnxObservation()
        item[keyPath: field] = invalid
        XCTAssertFalse(item.isValid, invalid)
      }
      item = onnxObservation()
      item[keyPath: field] = nil
      XCTAssertFalse(item.isValid)
    }
    item = onnxObservation()
    item.modelBackend = nil
    XCTAssertFalse(item.isValid)
    item = onnxObservation()
    item.imageBox = nil
    XCTAssertFalse(item.isValid)
    item = onnxObservation()
    item.imagePixelBox = nil
    XCTAssertFalse(item.isValid)
  }
  func testONNXImageBoxesAreFiniteOrderedAndBounded() {
    var item = onnxObservation()
    item.imageBox = [0, 0, 1, 1]
    item.imagePixelBox = [0, 0, 2048, 2048]
    XCTAssertTrue(item.isValid)
    let invalidNormalized: [[Double]] = [
      [], [0, 0, 1], [0, 0, 1, 1, 1], [-0.1, 0, 1, 1], [0, 0, 1.01, 1],
      [0.5, 0, 0.5, 1], [0, 1, 1, 0], [.nan, 0, 1, 1], [0, 0, .infinity, 1],
    ]
    for box in invalidNormalized {
      item = onnxObservation()
      item.imageBox = box
      XCTAssertFalse(item.isValid, "\(box)")
    }
    let invalidPixels: [[Double]] = [
      [], [0, 0, 200], [0, 0, 200, 200, 200], [-1, 0, 200, 200], [0, 0, 2049, 200],
      [20, 0, 20, 200], [0, 140, 200, 40], [0, .nan, 200, 200], [0, 0, 200, .infinity],
    ]
    for box in invalidPixels {
      item = onnxObservation()
      item.imagePixelBox = box
      XCTAssertFalse(item.isValid, "\(box)")
    }
  }
  func testInvalidONNXProvenanceCannotReplaceOrLoadMissionDocument() throws {
    let file = try file()
    var mission = MissionPlan(name: "Generated ONNX review")
    mission.brief = MissionBrief()
    mission.brief?.observations = [onnxObservation()]
    try MissionDocuments.save([mission], to: file)
    let before = try Data(contentsOf: file)
    mission.brief?.observations[0].modelContractSHA256 = "invalid"
    XCTAssertThrowsError(try MissionDocuments.save([mission], to: file))
    XCTAssertEqual(try Data(contentsOf: file), before)
    var document = try XCTUnwrap(
      JSONSerialization.jsonObject(with: before) as? [[String: Any]])
    var brief = try XCTUnwrap(document[0]["brief"] as? [String: Any])
    var observations = try XCTUnwrap(brief["observations"] as? [[String: Any]])
    observations[0]["modelBackend"] = "onnx-gpu"
    brief["observations"] = observations
    document[0]["brief"] = brief
    try JSONSerialization.data(withJSONObject: document).write(to: file)
    XCTAssertThrowsError(try MissionDocuments.load(from: file))
  }
  func testDanglingEvidenceAndAreaReferencesRejected() throws {
    var brief = MissionBrief()
    var observation = MissionObservation(
      title: "Object", category: "Unknown", coordinate: Coordinate(latitude: 44, longitude: 8)!)
    observation.evidenceID = UUID()
    brief.observations = [observation]
    XCTAssertFalse(brief.isValid(evidence: []))
    observation.evidenceID = nil
    observation.areaID = UUID()
    brief.observations = [observation]
    XCTAssertFalse(brief.isValid(evidence: []))
  }
  func testInvalidBriefCannotReplaceExistingDocument() throws {
    let file = try file()
    var mission = MissionPlan(name: "Mission")
    try MissionDocuments.save([mission], to: file)
    let before = try Data(contentsOf: file)
    mission.brief = MissionBrief()
    mission.brief?.version = 99
    XCTAssertThrowsError(try MissionDocuments.save([mission], to: file))
    XCTAssertEqual(try Data(contentsOf: file), before)
  }
  func testObservationScoresAreBoundedAndReviewStartsUnreviewed() {
    var item = MissionObservation(
      title: "Possible building", category: "Building",
      coordinate: Coordinate(latitude: 44, longitude: 8)!)
    XCTAssertEqual(item.state, .unreviewed)
    for invalid in [Double.nan, -.infinity, -0.1, 1.1] {
      item.modelScore = invalid
      XCTAssertFalse(item.isValid)
    }
    item.modelScore = 0.7
    XCTAssertTrue(item.isValid)
    item.bounds = [8, 44, 7, 45]
    XCTAssertFalse(item.isValid)
  }
  func testLiveMissionCatalogueClients() async throws {
    guard ProcessInfo.processInfo.environment["GODEYE_LIVE_CHECKS"] == "1" else {
      throw XCTSkip("Live mission catalogue checks are opt-in")
    }
    let bounds = try SentinelRegion(west: 9.16, south: 45.43, east: 9.22, north: 45.49)
    let end = Date()
    let start = end.addingTimeInterval(-14 * 86400)
    let optical = try await SentinelOpticalCatalog().search(
      SentinelOpticalQuery(region: bounds, start: start, end: end, maximumCloud: 100))
    let radar = try await SentinelCatalog().search(
      SentinelQuery(region: bounds, start: start, end: end, polarization: .both, mode: "IW"))
    XCTAssertFalse(optical.isEmpty)
    XCTAssertFalse(radar.scenes.isEmpty)
    print(
      "Mission live catalogue clients: \(optical.count) optical, \(radar.scenes.count) radar acquisitions"
    )
  }
  func testMonitorDuePauseAndClockRollback() {
    let now = Date(timeIntervalSince1970: 200000)
    var monitor = MissionMonitor(kind: .evidence)
    XCTAssertFalse(monitor.isDue(at: now))
    monitor.enabled = true
    XCTAssertTrue(monitor.isDue(at: now))
    monitor.lastAttempt = now
    XCTAssertFalse(monitor.isDue(at: now.addingTimeInterval(-3600)))
    XCTAssertFalse(monitor.isDue(at: now.addingTimeInterval(86399)))
    XCTAssertTrue(monitor.isDue(at: now.addingTimeInterval(86400)))
    monitor.enabled = false
    XCTAssertFalse(monitor.isDue(at: now.addingTimeInterval(172800)))
  }
  func testMonitorBaselineDedupAndFailureCheckpoint() {
    var monitor = MissionMonitor(kind: .sentinel2, areaID: UUID())
    let now = Date()
    XCTAssertEqual(monitor.receive(ids: ["a", "b", "b"], at: now, limited: false), 0)
    XCTAssertEqual(monitor.seenIDs, ["a", "b"])
    monitor.failure = "Network unavailable"
    monitor.lastAttempt = now.addingTimeInterval(10)
    XCTAssertEqual(monitor.lastSuccess, now)
    XCTAssertEqual(
      monitor.receive(ids: ["b", "c"], at: now.addingTimeInterval(100), limited: true), 1)
    XCTAssertNil(monitor.failure)
    XCTAssertTrue(monitor.summary.contains("limit"))
    XCTAssertEqual(
      monitor.receive(ids: ["a", "b", "c"], at: now.addingTimeInterval(200), limited: false), 0)
  }
  func testMonitorLimitsAndBoundedHistory() {
    var monitor = MissionMonitor(kind: .evidence)
    monitor.intervalHours = 0
    XCTAssertFalse(monitor.isValid)
    monitor.intervalHours = 24
    monitor.maximumCloud = .nan
    XCTAssertFalse(monitor.isValid)
    monitor.maximumCloud = 20
    _ = monitor.receive(ids: (0..<2100).map(String.init), at: Date(), limited: true)
    XCTAssertEqual(monitor.seenIDs.count, 2000)
    XCTAssertTrue(monitor.isValid)
  }
  func testVisionBoxGeoreferencingOrientationAndEdges() throws {
    let b = try XCTUnwrap(
      MissionImageGeometry.bounds(
        x: 0.25, y: 0.5, width: 0.5, height: 0.25, image: [8, 44, 10, 46]))
    XCTAssertEqual(b, [8.5, 45, 9.5, 45.5])
    XCTAssertEqual(
      MissionImageGeometry.bounds(x: 0, y: 0, width: 1, height: 1, image: [8, 44, 10, 46]),
      [8, 44, 10, 46])
    XCTAssertNil(
      MissionImageGeometry.bounds(x: -.infinity, y: 0, width: 1, height: 1, image: [8, 44, 10, 46]))
    XCTAssertNil(
      MissionImageGeometry.bounds(x: 0.5, y: 0, width: 0.6, height: 1, image: [8, 44, 10, 46]))
    XCTAssertNil(
      MissionImageGeometry.bounds(x: 0, y: 0, width: 0, height: 1, image: [8, 44, 10, 46]))
  }
  func testActivityAndUniqueObjectIDsAreBounded() {
    var brief = MissionBrief()
    for i in 0..<220 { brief.record("Event \(i)") }
    XCTAssertEqual(brief.activity.count, 200)
    XCTAssertEqual(brief.activity.first?.message, "Event 219")
    let item = MissionObservation(
      title: "Object", category: "Infrastructure",
      coordinate: Coordinate(latitude: 44, longitude: 8)!)
    brief.observations = [item, item]
    XCTAssertFalse(brief.isValid(evidence: []))
  }
  func testDossierValidationAndDeletionCleanup() throws {
    var mission = MissionPlan(name: "Generated dossier")
    let optical = MissionItem(
      kind: .optical, referenceID: UUID().uuidString, title: "Generated raster")
    var view = MissionItem(kind: .view, referenceID: UUID().uuidString, title: "Generated view")
    view.camera = .initial
    mission.items = [optical, view]
    var area = MissionArea(name: "Area", region: try region())
    var dossier = MissionDossier()
    dossier.linkedEvidenceIDs = [optical.id, view.id]
    dossier.baselineEvidenceIDs = [optical.id]
    area.dossier = dossier
    var brief = MissionBrief()
    brief.areas = [area]
    var observation = MissionObservation(
      title: "Generated object", category: "Infrastructure",
      coordinate: Coordinate(latitude: 44.01, longitude: 8.01)!)
    observation.areaID = area.id
    brief.observations = [observation]
    var task = MissionTask(title: "Follow up")
    task.areaID = area.id
    task.observationID = observation.id
    brief.tasks = [task]
    mission.brief = brief
    XCTAssertTrue(mission.isValid)
    mission.brief?.areas[0].dossier?.baselineEvidenceIDs = [view.id]
    XCTAssertFalse(mission.isValid)
    mission.brief = brief
    mission.brief?.areas[0].dossier?.linkedEvidenceIDs.append(optical.id)
    XCTAssertFalse(mission.isValid)
    mission.brief = brief
    mission.brief?.tasks[0].observationID = UUID()
    XCTAssertFalse(mission.isValid)
    mission.brief = brief
    mission.brief?.removeDossierEvidence(optical.id)
    mission.items.removeAll { $0.id == optical.id }
    XCTAssertTrue(mission.isValid)
    XCTAssertTrue(mission.brief?.areas[0].dossier?.baselineEvidenceIDs.isEmpty == true)
    mission.brief?.observations.removeAll { $0.id == observation.id }
    mission.brief?.removeObservationReferences(observation.id)
    XCTAssertTrue(mission.isValid)
    XCTAssertNil(mission.brief?.tasks[0].observationID)
  }
}
