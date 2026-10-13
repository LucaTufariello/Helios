import Foundation
import XCTest

@testable import GodEyeCore

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
    XCTAssertEqual(plan.brief?.observations.count, 1)
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
  }
}
