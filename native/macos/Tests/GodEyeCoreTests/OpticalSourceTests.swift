import Foundation
import XCTest

@testable import GodEyeCore

final class OpticalSourceTests: XCTestCase {
  func testCbersQueryUsesPublicL4AndDoesNotInventCloudFilter() throws {
    let region = try SentinelRegion(west: 8.539, south: 44.335, east: 8.558, north: 44.353)
    let start = try XCTUnwrap(ProviderParsers.date("2026-09-01T00:00:00Z"))
    let query = try SentinelOpticalQuery(
      region: region, start: start, end: start, maximumCloud: 20, source: .cbers4a)
    let url = try query.url()
    XCTAssertEqual(url.host, "data.inpe.br")
    let items = URLComponents(url: url, resolvingAgainstBaseURL: false)!.queryItems!
    XCTAssertEqual(items.first { $0.name == "collections" }?.value, "CB4A-WPM-L4-DN-1")
    XCTAssertFalse(items.contains { $0.name == "query" })
    XCTAssertThrowsError(
      try SentinelOpticalQuery(
        region: region, start: start, end: start, maximumCloud: 20, source: .prisma
      ).url())
  }
  func testRealCbersMetadataUnknownCloudAndFixedCOGAssets() throws {
    let fixture = try XCTUnwrap(
      Bundle.module.url(
        forResource: "cbers-catalog", withExtension: "json", subdirectory: "Fixtures"))
    let scenes = try SentinelOpticalScene.parse(Data(contentsOf: fixture), source: .cbers4a)
    let scene = try XCTUnwrap(scenes.first)
    XCTAssertEqual(scene.id, "CBERS_4A_WPM_20261003_198_130_L4")
    XCTAssertEqual(scene.sensor, .cbers4a)
    XCTAssertNil(scene.cloudCover)
    XCTAssertTrue(scene.isValid)
    XCTAssertEqual(
      scene.bands?["BAND0"],
      "https://brazil-eosats.s3.us-west-2.amazonaws.com/CBERS4A/WPM/198/130/CBERS_4A_WPM_20261003_198_130_L4/CBERS_4A_WPM_20261003_198_130_L4_BAND0.tif"
    )
    var altered = scene
    altered.bands?["BAND0"] = "file:///etc/passwd"
    XCTAssertFalse(altered.isValid)
    altered = scene
    altered.bands?["BAND1"] = nil
    XCTAssertFalse(altered.isValid)
    XCTAssertEqual(
      try SentinelOpticalScene.parse(Data("{\"features\":[]}".utf8), source: .cbers4a), [])
    XCTAssertEqual(SentinelOpticalScene.cbersAsset(sceneID: "../../escape", band: "BAND0"), "")
  }
  func testLegacySavedSentinel2RetainsDefaultSensor() throws {
    let url = try XCTUnwrap(
      Bundle.module.url(
        forResource: "sentinel2-result", withExtension: "json", subdirectory: "Fixtures"))
    let result = try JSONDecoder().decode(SentinelOpticalResult.self, from: Data(contentsOf: url))
    XCTAssertEqual(result.scene?.sensor, .sentinel2)
    XCTAssertTrue(result.isValid)
    XCTAssertNil(result.scientific)
  }
  func testPrismaProductAdmissionDoesNotAllowRawOrRemoteInputs() {
    let scene = SentinelOpticalScene(
      id: "PRS_L2D_STD_20260922102800_20260922102804_0001", acquiredAt: "2026-09-22T10:28:00Z",
      platform: "PRISMA", cloudCover: nil, bounds: [8.5, 44.3, 8.6, 44.4], visualURL: "",
      source: .prisma)
    XCTAssertTrue(scene.isValid)
    let raw = SentinelOpticalScene(
      id: "PRS_L1_STD_20260922102800_20260922102804_0001", acquiredAt: scene.acquiredAt,
      platform: "PRISMA", cloudCover: nil, bounds: scene.bounds, visualURL: "", source: .prisma)
    XCTAssertFalse(raw.isValid)
    let remote = SentinelOpticalScene(
      id: scene.id, acquiredAt: scene.acquiredAt, platform: "PRISMA", cloudCover: nil,
      bounds: scene.bounds, visualURL: "https://localhost", source: .prisma)
    XCTAssertFalse(remote.isValid)
  }
  func testNativePrismaWorkerAdapterPreservesSourceValuesAndState() async throws {
    let python =
      ProcessInfo.processInfo.environment["GODEYE_SAR_PYTHON"]
      ?? FileManager.default.homeDirectoryForCurrentUser.appendingPathComponent(
        "Library/Application Support/GodEye/SAR/runtime/bin/python3"
      ).path
    guard FileManager.default.isExecutableFile(atPath: python) else {
      throw XCTSkip("Optional raster runtime unavailable")
    }
    let product = try XCTUnwrap(
      Bundle.module.url(
        forResource: "PRS_L2D_GENERATED_20260922102800_0001", withExtension: "he5",
        subdirectory: "Fixtures"))
    let root = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
    defer { try? FileManager.default.removeItem(at: root) }
    try FileManager.default.createDirectory(at: root, withIntermediateDirectories: true)
    let request = root.appendingPathComponent("request.json")
    try JSONSerialization.data(withJSONObject: [
      "source": "prisma", "originalInput": product.path, "mode": "pan",
    ]).write(to: request)
    let worker = URL(fileURLWithPath: #filePath).deletingLastPathComponent()
      .deletingLastPathComponent().deletingLastPathComponent().appendingPathComponent(
        "Sources/GodEyeMac/Resources/Optical/sentinel2.py")
    let updates = OpticalProgressLog()
    let destination = root.appendingPathComponent("image")
    let result = try await SarProcessor(python: URL(fileURLWithPath: python), worker: worker)
      .renderOptical(request: request, destination: destination) { updates.append($0.state) }
    XCTAssertTrue(result.isValid)
    XCTAssertEqual(result.scene?.sensor, .prisma)
    XCTAssertEqual(result.scene?.cloudCover, 5)
    XCTAssertEqual(result.mode, "pan")
    XCTAssertEqual(result.sourceResolutionMetres, [5, 5])
    XCTAssertEqual(result.reflectanceScale, [0.1, 0.9])
    XCTAssertEqual(result.scientific, "source-values.tif")
    XCTAssertTrue(
      FileManager.default.fileExists(
        atPath: destination.appendingPathComponent("source-values.tif").path))
    XCTAssertFalse(
      FileManager.default.fileExists(
        atPath: destination.appendingPathComponent("selected.tmp.tif").path))
    XCTAssertTrue(FileManager.default.fileExists(atPath: product.path))
    let states = updates.values()
    XCTAssertEqual(states.first, .validating)
    XCTAssertTrue(states.contains(.processing))
    XCTAssertTrue(states.contains(.exporting))
    XCTAssertEqual(states.last, .completed)
    var invalid = result
    invalid.reflectanceScale = nil
    XCTAssertFalse(invalid.isValid)
    invalid = result
    invalid.scientific = "../outside.tif"
    XCTAssertFalse(invalid.isValid)
  }

}

private final class OpticalProgressLog: @unchecked Sendable {
  private let lock = NSLock()
  private var states: [SarState] = []
  func append(_ state: SarState) {
    lock.lock()
    defer { lock.unlock() }
    states.append(state)
  }
  func values() -> [SarState] {
    lock.lock()
    defer { lock.unlock() }
    return states
  }
}
