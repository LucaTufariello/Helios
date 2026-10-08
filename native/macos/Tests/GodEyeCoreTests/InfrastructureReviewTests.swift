import Foundation
import XCTest

@testable import GodEyeCore

final class InfrastructureReviewTests: XCTestCase {
  func testNativeAdapterComparesActualGeneratedRasterPair() async throws {
    let python =
      ProcessInfo.processInfo.environment["GODEYE_SAR_PYTHON"]
      ?? FileManager.default.homeDirectoryForCurrentUser
      .appendingPathComponent("Library/Application Support/GodEye/SAR/runtime/bin/python3").path
    guard FileManager.default.isExecutableFile(atPath: python) else {
      throw XCTSkip("The optional SAR raster environment is not installed.")
    }
    let package = URL(fileURLWithPath: #filePath).deletingLastPathComponent()
      .deletingLastPathComponent().deletingLastPathComponent()
    let root = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
    defer { try? FileManager.default.removeItem(at: root) }
    let fixture = package.appendingPathComponent("Validation/create_change_review_fixture.py")
    let status = try await Task.detached {
      let process = Process()
      process.executableURL = URL(fileURLWithPath: python)
      process.arguments = ["-B", fixture.path, "--output", root.path]
      process.standardOutput = FileHandle.nullDevice
      process.standardError = FileHandle.nullDevice
      try process.run()
      process.waitUntilExit()
      return process.terminationStatus
    }.value
    XCTAssertEqual(status, 0)
    let sites = try SatelliteDocuments.loadSites(
      from: root.appendingPathComponent("Satellite/sites.json"))
    XCTAssertEqual(sites.count, 1)
    let request = root.appendingPathComponent("comparison-request.json")
    let worker = package.appendingPathComponent("Sources/GodEyeMac/Resources/SAR/sentinel1.py")
    let processor = SarProcessor(python: URL(fileURLWithPath: python), worker: worker)
    let destination = root.appendingPathComponent("Satellite/reviews/\(UUID().uuidString)")
    let result = try await processor.compare(request: request, destination: destination) { _ in }
    XCTAssertTrue(result.isValid)
    XCTAssertEqual(result.state, .completed)
    XCTAssertEqual(result.candidates?.count, 2)
    XCTAssertEqual(result.site?.id, sites[0].id)
    XCTAssertTrue(
      FileManager.default.fileExists(
        atPath: destination.appendingPathComponent("delta-db.tif").path))
    XCTAssertTrue(
      FileManager.default.fileExists(atPath: destination.appendingPathComponent("Change.png").path))
  }
  func testWorkerReportDecodesAndRetainsPortableScientificProvenance() throws {
    let file = try XCTUnwrap(
      Bundle.module.url(
        forResource: "change-review-result", withExtension: "json", subdirectory: "Fixtures"))
    let report = try SarProcessor.read(ChangeReviewResult.self, from: file)
    XCTAssertTrue(report.isValid)
    XCTAssertEqual(report.state, .completed)
    XCTAssertEqual(report.candidates?.count, 2)
    XCTAssertEqual(report.crs, "EPSG:4326")
    XCTAssertTrue(report.quantity?.contains("log ratio") ?? false)
    XCTAssertEqual(report.evidence?.first?.alignedSitePowerSHA256.count, 64)
    XCTAssertEqual(report.evidence?.first?.engine["version"], "fixture")
    let encoded = try JSONEncoder().encode(report)
    let decoded = try JSONDecoder().decode(ChangeReviewResult.self, from: encoded)
    XCTAssertEqual(decoded.limitations, report.limitations)
    XCTAssertTrue(decoded.isValid)
  }
  private func acquisition(_ date: String) throws -> SarResult {
    let json = """
      {"version":1,"inputName":"GENERATED.SAFE","createdAt":"2026-10-04T12:00:00Z","state":"completed",
       "metadata":{"mission":"Sentinel-1","satellite":"Sentinel-1A","productType":"GRD","mode":"IW",
        "acquisitionStart":"\(date)","acquisitionEnd":"\(date)","orbitDirection":"DESCENDING","relativeOrbit":66,
        "polarizations":["VV"],"bands":[],"inputName":"GENERATED.SAFE"},
       "options":{"polarizations":["VV"],"applyOrbit":true,"speckle":false,"pixelSpacing":20,"lowPercentile":2,"highPercentile":98},
       "outputs":[{"id":"VV","preview":"VV-preview.png","visualization":"VV-display.tif","scientific":"VV-sigma0.tif",
         "bounds":[9,45,9.01,45.01],"displayRanges":[[-20,0]],"width":100,"height":100}]}
      """
    return try JSONDecoder().decode(SarResult.self, from: Data(json.utf8))
  }
  func testCompatiblePairAndUTCTimestamps() throws {
    let before = try acquisition("2026-09-01T05:00:00.123456")
    let after = try acquisition("2026-09-13T05:00:00.123456Z")
    XCTAssertNil(ChangeReviewCompatibility.issue(before: before, after: after, polarization: "VV"))
    XCTAssertNotNil(
      ChangeReviewCompatibility.issue(before: before, after: before, polarization: "VV"))
    XCTAssertNotNil(
      ChangeReviewCompatibility.issue(before: after, after: before, polarization: "VV"))
    XCTAssertNotNil(
      ChangeReviewCompatibility.issue(before: before, after: after, polarization: "VH"))
    XCTAssertNotNil(
      ChangeReviewCompatibility.issue(before: before, after: after, polarization: "Composite"))
  }
  func testGeometryAndProcessingSettingsGate() throws {
    let before = try acquisition("2026-09-01T05:00:00Z")
    let original = try acquisition("2026-09-13T05:00:00Z")
    var after = original
    after.metadata?.relativeOrbit = 67
    XCTAssertNotNil(
      ChangeReviewCompatibility.issue(before: before, after: after, polarization: "VV"))
    after = original
    after.metadata?.satellite = "Sentinel-1B"
    XCTAssertNotNil(
      ChangeReviewCompatibility.issue(before: before, after: after, polarization: "VV"))
    after = original
    after.metadata?.relativeOrbit = nil
    XCTAssertNotNil(
      ChangeReviewCompatibility.issue(before: before, after: after, polarization: "VV"))
    after = original
    after.options?.speckle = true
    XCTAssertNotNil(
      ChangeReviewCompatibility.issue(before: before, after: after, polarization: "VV"))
    after = original
    after.options?.pixelSpacing = 40
    XCTAssertNotNil(
      ChangeReviewCompatibility.issue(before: before, after: after, polarization: "VV"))
    after = original
    after.state = .failed
    XCTAssertNotNil(
      ChangeReviewCompatibility.issue(before: before, after: after, polarization: "VV"))
  }
  func testSiteStorageRoundTripAndPreservesUnreadableDocument() throws {
    let root = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
    defer { try? FileManager.default.removeItem(at: root) }
    let file = root.appendingPathComponent("sites.json")
    XCTAssertTrue(try SatelliteDocuments.loadSites(from: file).isEmpty)
    let site = InfrastructureSite(
      name: "  Industrial area  ", kind: "Industrial site",
      region: try SentinelRegion(west: 9, south: 45, east: 9.01, north: 45.01))
    XCTAssertEqual(site.name, "Industrial area")
    try SatelliteDocuments.saveSites([site], to: file)
    XCTAssertEqual(try SatelliteDocuments.loadSites(from: file), [site])
    XCTAssertThrowsError(try SatelliteDocuments.saveSites([site, site], to: file))
    let corrupted = Data("broken".utf8)
    try corrupted.write(to: file)
    XCTAssertThrowsError(try SatelliteDocuments.saveSites([site], to: file))
    XCTAssertEqual(try Data(contentsOf: file), corrupted)
  }
  func testNotesAreBoundedAndPersistDecisionWithReference() throws {
    let file = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
    defer { try? FileManager.default.removeItem(at: file) }
    let id = UUID()
    var notes = ChangeReviewNotes(reviewID: id)
    var decision = CandidateReview()
    decision.decision = .uncertain
    decision.note = "Needs dated optical confirmation."
    decision.reference = "https://example.org/reference"
    notes.candidates["1"] = decision
    XCTAssertTrue(notes.isValid)
    try SatelliteDocuments.save(notes, to: file)
    let loaded = try SarProcessor.read(ChangeReviewNotes.self, from: file)
    XCTAssertEqual(loaded.reviewID, id)
    XCTAssertEqual(loaded.candidates["1"]?.decision, .uncertain)
    XCTAssertEqual(loaded.candidates["1"]?.reference, decision.reference)
    notes.candidates["../escape"] = decision
    XCTAssertFalse(notes.isValid)
    notes.candidates.removeValue(forKey: "../escape")
    notes.candidates["1"]?.note = String(repeating: "x", count: 4001)
    XCTAssertFalse(notes.isValid)
  }
  func testMeasurementOptionsRejectUnsafeAndUnsupportedValues() {
    var options = ChangeReviewOptions()
    XCTAssertTrue(options.isValid)
    options.thresholdDb = .nan
    XCTAssertFalse(options.isValid)
    options.thresholdDb = 3
    options.averagingWindow = 4
    XCTAssertFalse(options.isValid)
    options.averagingWindow = 5
    options.minimumPixels = 1
    XCTAssertFalse(options.isValid)
  }
  func testCandidateAdmissionRejectsInvalidBoundsAndNumbers() throws {
    let json = """
      {"id":"1","direction":"increase","bounds":[9,45,9.01,45.01],"pixels":25,
       "approximateAreaM2":10000,"meanDeltaDb":3.5,"maximumAbsoluteDeltaDb":5}
      """
    var candidate = try JSONDecoder().decode(ChangeCandidate.self, from: Data(json.utf8))
    XCTAssertTrue(candidate.isValid)
    candidate.bounds = [170, 45, -170, 46]
    XCTAssertFalse(candidate.isValid)
    candidate.bounds = [9, 45, 9.01, 45.01]
    candidate.meanDeltaDb = .infinity
    XCTAssertFalse(candidate.isValid)
    candidate.meanDeltaDb = 3.5
    candidate.id = "../escape"
    XCTAssertFalse(candidate.isValid)
  }
  func testCompletedReportRequiresEvidenceAndValidScientificExtent() throws {
    let json = """
      {"version":1,"createdAt":"2026-10-04T12:00:00Z","state":"completed"}
      """
    var report = try JSONDecoder().decode(ChangeReviewResult.self, from: Data(json.utf8))
    XCTAssertFalse(report.isValid)
    report.state = .failed
    XCTAssertTrue(report.isValid)
    report.version = 999
    XCTAssertFalse(report.isValid)
  }
}
