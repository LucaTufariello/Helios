import Foundation
import XCTest

@testable import HeliosCore

final class CrossPlatformMissionTests: XCTestCase {
  func testQtAuthoredMissionRemainsReadableAndWritableBySwift() throws {
    let fixture = try XCTUnwrap(
      Bundle.module.url(
        forResource: "cross-platform-mission", withExtension: "json", subdirectory: "Fixtures"))
    let plans = try MissionDocuments.load(from: fixture)
    let plan = try XCTUnwrap(plans.first)
    XCTAssertEqual(plan.name, "GENERATED QA — desktop infrastructure review")
    XCTAssertEqual(plan.brief?.areas.count, 1)
    XCTAssertEqual(plan.brief?.notes.count, 1)
    XCTAssertEqual(plan.brief?.tasks.count, 1)
    XCTAssertEqual(plan.brief?.observations.count, 2)
    let proposal = try XCTUnwrap(plan.brief?.observations.last)
    XCTAssertEqual(proposal.method, "ONNX CPU proposal · GENERATED-QA-detector.onnx")
    XCTAssertEqual(proposal.modelSHA256, String(repeating: "b", count: 64))
    XCTAssertEqual(proposal.modelContractSHA256, String(repeating: "c", count: 64))
    XCTAssertEqual(proposal.modelBackend, "onnx-cpu")
    XCTAssertEqual(proposal.imageBox, [0.1, 0.2, 0.6, 0.7])
    XCTAssertEqual(proposal.imagePixelBox, [20, 40, 120, 140])
    XCTAssertEqual(proposal.observedAt?.timeIntervalSince1970, 1_791_892_800)
    XCTAssertEqual(proposal.state, .unreviewed)
    XCTAssertTrue(plan.items.contains { $0.kind == .view })
    let optical = try XCTUnwrap(plan.items.first { $0.kind == .optical })
    XCTAssertEqual(optical.acquisition?.sourceID, "GENERATED_QA_NOT_A_REAL_SATELLITE_ACQUISITION")
    XCTAssertEqual(optical.acquisition?.displayBand, "RGB")
    XCTAssertEqual(optical.acquisition?.acquiredAt?.timeIntervalSince1970, 1_791_892_800)
    let directory = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
    try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
    defer { try? FileManager.default.removeItem(at: directory) }
    let copy = directory.appendingPathComponent("plans.json")
    try MissionDocuments.save(plans, to: copy)
    XCTAssertEqual(try MissionDocuments.load(from: copy), plans)
    // Inspect the document too: typed equality alone would miss fields dropped during decode.
    let encoded = try XCTUnwrap(
      JSONSerialization.jsonObject(with: Data(contentsOf: copy)) as? [[String: Any]])
    let brief = try XCTUnwrap(encoded.first?["brief"] as? [String: Any])
    let observations = try XCTUnwrap(brief["observations"] as? [[String: Any]])
    for key in ["modelSHA256", "modelContractSHA256", "modelBackend", "imageBox", "imagePixelBox"] {
      XCTAssertNil(observations.first?[key])
    }
    let savedProposal = try XCTUnwrap(observations.last)
    XCTAssertEqual(savedProposal["modelSHA256"] as? String, String(repeating: "b", count: 64))
    XCTAssertEqual(
      savedProposal["modelContractSHA256"] as? String, String(repeating: "c", count: 64))
    XCTAssertEqual(savedProposal["modelBackend"] as? String, "onnx-cpu")
    XCTAssertEqual(savedProposal["imageBox"] as? [Double], [0.1, 0.2, 0.6, 0.7])
    XCTAssertEqual(savedProposal["imagePixelBox"] as? [Double], [20, 40, 120, 140])
  }
  func testDossierFixturePreservesLinksPinsAndTaskOrigin() throws {
    let fixture = try XCTUnwrap(
      Bundle.module.url(
        forResource: "cross-platform-dossier", withExtension: "json", subdirectory: "Fixtures"))
    let plans = try MissionDocuments.load(from: fixture)
    let plan = try XCTUnwrap(plans.first)
    let brief = try XCTUnwrap(plan.brief)
    let dossier = try XCTUnwrap(brief.areas.first?.dossier)
    XCTAssertEqual(dossier.linkedEvidenceIDs.count, 2)
    let optical = try XCTUnwrap(plan.items.first { $0.kind == .optical })
    XCTAssertEqual(dossier.baselineEvidenceIDs, [optical.id])
    XCTAssertEqual(dossier.sourceSiteEvidenceID, plan.items.first { $0.kind == .site }?.id)
    XCTAssertEqual(brief.tasks.first?.observationID, brief.observations.first?.id)
    let directory = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
    try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
    defer { try? FileManager.default.removeItem(at: directory) }
    let copy = directory.appendingPathComponent("plans.json")
    try MissionDocuments.save(plans, to: copy)
    XCTAssertEqual(try MissionDocuments.load(from: copy), plans)
    let raw = try XCTUnwrap(
      JSONSerialization.jsonObject(with: Data(contentsOf: copy)) as? [[String: Any]])
    let savedBrief = try XCTUnwrap(raw.first?["brief"] as? [String: Any])
    let areas = try XCTUnwrap(savedBrief["areas"] as? [[String: Any]])
    let savedDossier = try XCTUnwrap(areas.first?["dossier"] as? [String: Any])
    XCTAssertEqual(savedDossier["version"] as? Int, 1)
    XCTAssertEqual(
      savedDossier["linkedEvidenceIDs"] as? [String], dossier.linkedEvidenceIDs.map(\.uuidString))
    XCTAssertEqual(
      savedDossier["baselineEvidenceIDs"] as? [String],
      dossier.baselineEvidenceIDs.map(\.uuidString))
    XCTAssertEqual(
      savedDossier["sourceSiteEvidenceID"] as? String, dossier.sourceSiteEvidenceID?.uuidString)
    let tasks = try XCTUnwrap(savedBrief["tasks"] as? [[String: Any]])
    XCTAssertEqual(
      tasks.first?["observationID"] as? String, brief.tasks.first?.observationID?.uuidString)
  }
  func testMonitoringFixturePreservesConfigurationInboxAndTaskOrigin() throws {
    let fixture = try XCTUnwrap(
      Bundle.module.url(
        forResource: "cross-platform-monitoring", withExtension: "json", subdirectory: "Fixtures"))
    let plans = try MissionDocuments.load(from: fixture)
    let plan = try XCTUnwrap(plans.first)
    let brief = try XCTUnwrap(plan.brief)
    let monitoring = try XCTUnwrap(brief.areas.first?.dossier?.monitoring)
    XCTAssertFalse(monitoring.enabled)
    XCTAssertEqual(monitoring.controls.count, 6)
    XCTAssertEqual(monitoring.maximumResidualPixels, 0.5)
    XCTAssertEqual(brief.monitoringInbox?.count, 1)
    XCTAssertEqual(brief.tasks.first?.monitoringEventID, brief.monitoringInbox?.first?.id)
    let directory = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
    try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
    defer { try? FileManager.default.removeItem(at: directory) }
    let copy = directory.appendingPathComponent("plans.json")
    try MissionDocuments.save(plans, to: copy)
    XCTAssertEqual(try MissionDocuments.load(from: copy), plans)
    let raw = try XCTUnwrap(
      JSONSerialization.jsonObject(with: Data(contentsOf: copy)) as? [[String: Any]])
    let saved = try XCTUnwrap(raw.first?["brief"] as? [String: Any])
    XCTAssertEqual(
      (saved["monitoringInbox"] as? [[String: Any]])?.first?["status"] as? String, "Blocked")
    XCTAssertNotNil((saved["areas"] as? [[String: Any]])?.first?["dossier"])
    var changed = plan
    let id = try XCTUnwrap(monitoring.baselineEvidenceID)
    changed.brief?.removeDossierEvidence(id)
    changed.items.removeAll { $0.id == id }
    XCTAssertTrue(changed.isValid)
    XCTAssertNil(changed.brief?.monitoringInbox?.first?.baselineEvidenceID)
    XCTAssertFalse(changed.brief?.areas.first?.dossier?.monitoring?.enabled ?? true)
    var wrongMonitor = plan
    wrongMonitor.brief?.monitors[0].kind = .evidence
    XCTAssertFalse(wrongMonitor.isValid)
  }

}
