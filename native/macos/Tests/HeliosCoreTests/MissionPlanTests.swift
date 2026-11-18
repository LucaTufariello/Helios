import Foundation
import XCTest

@testable import HeliosCore

final class MissionPlanTests: XCTestCase {
  private func file() throws -> URL {
    let root = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
    try FileManager.default.createDirectory(at: root, withIntermediateDirectories: true)
    addTeardownBlock { try? FileManager.default.removeItem(at: root) }
    return root.appendingPathComponent("plans.json")
  }
  func testMissionRoundTripRetainsCameraSitesNotesAndEvidenceReferences() throws {
    let file = try file()
    var mission = MissionPlan(name: "  Port review  ")
    mission.objective = "Compare dated acquisitions at a named site."
    mission.notes = "A human-reviewed observation, not a validated classification."
    let camera = Camera(
      center: Coordinate(latitude: 45, longitude: 9)!, altitude: 8000, heading: 90, tilt: 35)
    var view = MissionItem(kind: .view, referenceID: UUID().uuidString, title: "View")
    view.camera = camera
    let site = InfrastructureSite(
      name: "Port", kind: "Port",
      region: try SentinelRegion(west: 9, south: 45, east: 9.01, north: 45.01))
    var link = MissionItem(kind: .site, referenceID: site.id.uuidString, title: site.name)
    link.site = site
    let optical = MissionItem(kind: .optical, referenceID: UUID().uuidString, title: "Dated image")
    let radar = MissionItem(kind: .radar, referenceID: UUID().uuidString, title: "SAR acquisition")
    mission.items = [view, link, optical, radar]
    try MissionDocuments.save([mission], to: file)
    XCTAssertEqual(try MissionDocuments.load(from: file), [mission])
    XCTAssertEqual(mission.name, "Port review")
    XCTAssertEqual(mission.items[0].camera, camera)
    // References are data identifiers, not executable paths or copied large rasters.
    let data = String(decoding: try Data(contentsOf: file), as: UTF8.self)
    XCTAssertFalse(data.contains("pythonPath"))
    XCTAssertFalse(data.contains("gptPath"))
    XCTAssertFalse(data.contains("password"))
  }
  func testRecordSnapshotPreservesSourceTimestampAndOmitsTransientMediaLinks() throws {
    let time = Date(timeIntervalSince1970: 1000.123456)
    let source = GeoRecord(
      id: "qa:1", feed: .installations, label: "Generated fixture",
      coordinate: Coordinate(latitude: 45, longitude: 9)!, altitude: nil, observedAt: time,
      source: "QA fixture",
      details: [
        "Frame URL": "https://example.com/?token=secret",
        "Video URL": "https://example.com/private", "Type": "Infrastructure",
      ])
    let snapshot = MissionRecord(source)
    XCTAssertTrue(snapshot.isValid)
    XCTAssertEqual(snapshot.observedAt, time)
    XCTAssertEqual(snapshot.details, ["Type": "Infrastructure"])
    var item = MissionItem(kind: .record, referenceID: source.id, title: source.label)
    item.record = snapshot
    XCTAssertTrue(item.isValid)
    var mission = MissionPlan(name: "Timestamp round trip")
    mission.items = [item]
    let file = try file()
    try MissionDocuments.save([mission], to: file)
    XCTAssertEqual(try MissionDocuments.load(from: file), [mission])
    item.referenceID = "wrong-id"
    XCTAssertFalse(item.isValid)
  }
  func testInvalidMissionOrDuplicateIdentityDoesNotOverwriteSavedLibrary() throws {
    let file = try file()
    var mission = MissionPlan(name: "Mission")
    try MissionDocuments.save([mission], to: file)
    let original = try Data(contentsOf: file)
    mission.notes = String(repeating: "a", count: 32_001)
    XCTAssertThrowsError(try MissionDocuments.save([mission], to: file))
    XCTAssertEqual(try Data(contentsOf: file), original)
    mission.notes = ""
    XCTAssertThrowsError(try MissionDocuments.save([mission, mission], to: file))
    XCTAssertEqual(try Data(contentsOf: file), original)
    mission.name = "  "
    XCTAssertFalse(mission.isValid)
  }
  func testCorruptPreviousLibraryIsPreserved() throws {
    let file = try file()
    let corrupt = Data("invalid previous library".utf8)
    try corrupt.write(to: file)
    XCTAssertThrowsError(try MissionDocuments.save([MissionPlan(name: "New")], to: file))
    XCTAssertEqual(try Data(contentsOf: file), corrupt)
  }
  func testSnapshotSurvivesDeletionOfSavedSiteAndImageLibraryReference() throws {
    let file = try file()
    var mission = MissionPlan(name: "Review")
    let site = InfrastructureSite(
      name: "Site", kind: "Infrastructure",
      region: try SentinelRegion(west: 9, south: 45, east: 9.01, north: 45.01))
    var item = MissionItem(kind: .site, referenceID: site.id.uuidString, title: site.name)
    item.site = site
    mission.items = [
      item,
      MissionItem(
        kind: .optical, referenceID: UUID().uuidString, title: "Unavailable image reference"),
    ]
    try MissionDocuments.save([mission], to: file)
    let sites = file.deletingLastPathComponent().appendingPathComponent("sites.json")
    try SatelliteDocuments.saveSites([site], to: sites)
    _ = try SatelliteDocuments.deleteSite(id: site.id, from: sites)
    let restored = try XCTUnwrap(MissionDocuments.load(from: file).first)
    XCTAssertEqual(restored.items[0].site, site)
    XCTAssertTrue(restored.isValid)
  }
  func testInvalidAssetReferencesAndDuplicateItemsAreRejected() {
    XCTAssertFalse(
      MissionItem(kind: .optical, referenceID: "../../elsewhere", title: "Unsafe").isValid)
    XCTAssertFalse(MissionItem(kind: .radar, referenceID: "/tmp/file", title: "Unsafe").isValid)
    var mission = MissionPlan(name: "Mission")
    let item = MissionItem(kind: .optical, referenceID: UUID().uuidString, title: "Image")
    mission.items = [item, item]
    XCTAssertFalse(mission.isValid)
    XCTAssertFalse(
      MissionItem(kind: .view, referenceID: UUID().uuidString, title: "Missing camera").isValid)
  }
  func testSavedComparisonRetainsPairBlendAndRejectsInvalidReferences() throws {
    let file = try file()
    var mission = MissionPlan(name: "Saved comparison")
    mission.comparison = MissionComparison(
      baseID: "optical:\(UUID().uuidString)", topID: "radar:\(UUID().uuidString):VV", blend: 0.25)
    try MissionDocuments.save([mission], to: file)
    XCTAssertEqual(try MissionDocuments.load(from: file), [mission])
    mission.comparison?.blend = .nan
    XCTAssertFalse(mission.isValid)
    mission.comparison?.blend = 1.1
    XCTAssertFalse(mission.isValid)
    mission.comparison?.blend = 0.5
    mission.comparison?.baseID = "optical:../../file"
    XCTAssertFalse(mission.isValid)
    mission.comparison?.baseID = ""
    XCTAssertTrue(mission.isValid)
  }
  func testComparisonRequiresValidOverlappingBounds() {
    XCTAssertTrue(RasterComparison.overlaps([9, 45, 10, 46], [9.5, 45.5, 10.5, 46.5]))
    XCTAssertFalse(RasterComparison.overlaps([9, 45, 10, 46], [11, 45, 12, 46]))
    XCTAssertFalse(RasterComparison.overlaps([9, 45, 10, 46], [10, 45, 11, 46]))
    XCTAssertFalse(RasterComparison.overlaps([], [9, 45, 10, 46]))
    XCTAssertFalse(RasterComparison.overlaps([9, 45, Double.nan, 46], [9, 45, 10, 46]))
  }
}
