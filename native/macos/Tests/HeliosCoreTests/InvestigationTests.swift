import Foundation
import XCTest

@testable import HeliosCore

final class InvestigationTests: XCTestCase {
  private func fixture(_ name: String) throws -> URL {
    try XCTUnwrap(
      Bundle.module.url(forResource: name, withExtension: "json", subdirectory: "Fixtures"))
  }
  func testQtInvestigationRoundTripRetainsSnapshotsReviewsAndDependencies() throws {
    let plans = try MissionDocuments.load(from: fixture("cross-platform-investigation"))
    let investigation = try XCTUnwrap(plans.first?.brief?.investigation)
    XCTAssertEqual(investigation.snapshots.count, 3)
    XCTAssertEqual(investigation.findings.count, 1)
    XCTAssertEqual(investigation.findings.first?.state, "Supported")
    XCTAssertEqual(investigation.findings.first?.revisions.count, 1)
    XCTAssertEqual(investigation.findings.first?.revisions.first?.state, "Supported")
    XCTAssertTrue(investigation.findings[0].citations.contains { $0.role == "Contradicting" })
    XCTAssertEqual(plans.first?.brief?.tasks.last?.dependencyIDs?.count, 1)
    XCTAssertEqual(investigation.snapshots.last?.relationship?.kind, "Connected to")
    let directory = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
    defer { try? FileManager.default.removeItem(at: directory) }
    let copy = directory.appendingPathComponent("missions.json")
    try MissionDocuments.save(plans, to: copy)
    XCTAssertEqual(try MissionDocuments.load(from: copy), plans)
    var removed = plans
    for item in removed[0].items { removed[0].brief?.removeDossierEvidence(item.id) }
    removed[0].items = []
    XCTAssertTrue(removed[0].isValid)
    XCTAssertEqual(removed[0].brief?.investigation, investigation)
  }
  func testPrerequisiteCyclesAndPrematureCompletionAreRejected() {
    var first = MissionTask(title: "Verify source")
    var second = MissionTask(title: "Conclude")
    second.dependencyIDs = [first.id]
    XCTAssertTrue(validInvestigationTaskDependencies([first, second]))
    second.done = true
    XCTAssertFalse(validInvestigationTaskDependencies([first, second]))
    first.done = true
    XCTAssertTrue(validInvestigationTaskDependencies([first, second]))
    first.dependencyIDs = [second.id]
    XCTAssertFalse(validInvestigationTaskDependencies([first, second]))
    XCTAssertFalse(validInvestigationTaskDependencies([second]))
  }
  func testRegistryConflictArchiveAndDeletionProtection() throws {
    let registry = try InvestigationDocuments.loadRegistry(from: fixture("cross-platform-registry"))
    XCTAssertTrue(registry.isValid)
    let directory = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
    defer { try? FileManager.default.removeItem(at: directory) }
    let url = directory.appendingPathComponent("registry.json")
    try InvestigationDocuments.saveRegistry(registry, to: url, expectedData: nil)
    let initial = try Data(contentsOf: url)
    var revised = registry
    revised.assets[0].name = "Amended registry name"
    revised.assets[0].revision += 1
    try InvestigationDocuments.saveRegistry(revised, to: url, expectedData: initial)
    XCTAssertThrowsError(
      try InvestigationDocuments.saveRegistry(registry, to: url, expectedData: initial))
    XCTAssertTrue(FileManager.default.fileExists(atPath: url.path + ".backup"))
    let current = try Data(contentsOf: url)
    var erased = revised
    erased.assets = []
    erased.relationships = []
    XCTAssertThrowsError(
      try InvestigationDocuments.saveRegistry(erased, to: url, expectedData: current))
    revised.assets[0].archived = true
    revised.assets[0].revision += 1
    try InvestigationDocuments.saveRegistry(revised, to: url, expectedData: current)
    XCTAssertTrue(try InvestigationDocuments.loadRegistry(from: url).assets[0].archived)
    let plans = try MissionDocuments.load(from: fixture("cross-platform-investigation"))
    XCTAssertFalse(try XCTUnwrap(plans[0].brief?.investigation?.snapshots[0].asset).archived)
  }
}
