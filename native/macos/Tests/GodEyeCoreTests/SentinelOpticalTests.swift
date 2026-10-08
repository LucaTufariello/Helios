import Foundation
import XCTest

@testable import GodEyeCore

final class SentinelOpticalTests: XCTestCase {
  func fixture(_ name: String) throws -> Data {
    try Data(
      contentsOf: XCTUnwrap(
        Bundle.module.url(forResource: name, withExtension: "json", subdirectory: "Fixtures")))
  }
  func testQueryUsesPublicEndpointWithUTCDatesAndCloudLimit() throws {
    let region = try SentinelRegion(west: 9.16, south: 45.43, east: 9.22, north: 45.49)
    let start = try XCTUnwrap(ProviderParsers.date("2026-09-01T18:00:00Z"))
    let end = try XCTUnwrap(ProviderParsers.date("2026-09-30T18:00:00Z"))
    let query = try SentinelOpticalQuery(region: region, start: start, end: end, maximumCloud: 20)
    let url = try query.url()
    XCTAssertEqual(url.host, "earth-search.aws.element84.com")
    let items = Dictionary(
      uniqueKeysWithValues: URLComponents(url: url, resolvingAgainstBaseURL: false)!.queryItems!.map
      { ($0.name, $0.value!) })
    XCTAssertEqual(items["collections"], "sentinel-2-c1-l2a")
    XCTAssertEqual(items["sortby"], "-properties.datetime")
    XCTAssertEqual(items["datetime"], "2026-09-01T00:00:00.000Z/2026-09-30T23:59:59.999Z")
    XCTAssertTrue(items["query"]!.contains("20"))
    XCTAssertThrowsError(
      try SentinelOpticalQuery(region: region, start: end, end: start, maximumCloud: 20))
    XCTAssertThrowsError(
      try SentinelOpticalQuery(region: region, start: start, end: end, maximumCloud: .nan))
    XCTAssertThrowsError(
      try SentinelOpticalQuery(
        region: region, start: start, end: start.addingTimeInterval(367 * 86400), maximumCloud: 20))
  }
  func testRealCatalogueMetadataAndTrustedAssetAdmission() throws {
    let scenes = try SentinelOpticalScene.parse(fixture("sentinel2-catalog"))
    let scene = try XCTUnwrap(scenes.first)
    XCTAssertEqual(scenes.count, 1)
    XCTAssertEqual(scene.id, "S2C_T32TNR_20260927T102053_L2A")
    XCTAssertEqual(scene.platform, "sentinel-2c")
    XCTAssertTrue(scene.isValid)
    XCTAssertLessThan(try XCTUnwrap(scene.cloudCover), 1)
    for url in [
      "file:///etc/passwd", scene.visualURL + "?key=x",
      scene.visualURL.replacingOccurrences(of: "https://", with: "https://user@"),
      scene.visualURL.replacingOccurrences(of: "TCI.tif", with: "../TCI.tif"),
    ] {
      XCTAssertFalse(SentinelOpticalScene.validAsset(url, sceneID: scene.id))
    }
  }
  func testMalformedAndUntrustedCatalogueEntriesAreRejected() throws {
    XCTAssertThrowsError(try SentinelOpticalScene.parse(Data("{}".utf8)))
    let original = String(decoding: try fixture("sentinel2-catalog"), as: UTF8.self)
    let altered = original.replacingOccurrences(
      of: "e84-earth-search-sentinel-data.s3.us-west-2.amazonaws.com", with: "localhost")
    XCTAssertEqual(try SentinelOpticalScene.parse(Data(altered.utf8)).count, 0)
    XCTAssertThrowsError(
      try SentinelOpticalScene.parse(Data(repeating: 0, count: 8 * 1024 * 1024 + 1)))
  }
  func testRealWorkerResultAdmissionAndProvenanceRoundTrip() throws {
    var result = try JSONDecoder().decode(
      SentinelOpticalResult.self, from: fixture("sentinel2-result"))
    XCTAssertTrue(result.isValid)
    XCTAssertEqual(result.crs, "EPSG:4326")
    XCTAssertEqual(result.sourceResolutionMetres, [10, 10])
    XCTAssertTrue(result.limitations!.contains { $0.contains("not a scientific reflectance") })
    XCTAssertTrue(
      try JSONDecoder().decode(SentinelOpticalResult.self, from: JSONEncoder().encode(result))
        .isValid)
    result.preview = "../escape.png"
    XCTAssertFalse(result.isValid)
    result.preview = "preview.png"
    result.width = 2049
    XCTAssertFalse(result.isValid)
  }
  func testNativeWorkerAdapterReportsInvalidURLAndCleansUp() async throws {
    let python =
      ProcessInfo.processInfo.environment["GODEYE_SAR_PYTHON"]
      ?? FileManager.default.homeDirectoryForCurrentUser.appendingPathComponent(
        "Library/Application Support/GodEye/SAR/runtime/bin/python3"
      ).path
    guard FileManager.default.isExecutableFile(atPath: python) else {
      throw XCTSkip("Optional Python raster environment unavailable.")
    }
    let root = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
    defer { try? FileManager.default.removeItem(at: root) }
    try FileManager.default.createDirectory(at: root, withIntermediateDirectories: true)
    let result = try JSONDecoder().decode(
      SentinelOpticalResult.self, from: fixture("sentinel2-result"))
    let request = SentinelOpticalRequest(scene: result.scene!, region: result.region!)
    var json = try XCTUnwrap(
      JSONSerialization.jsonObject(with: JSONEncoder().encode(request)) as? [String: Any])
    var scene = json["scene"] as! [String: Any]
    scene["visualURL"] = "file:///etc/passwd"
    json["scene"] = scene
    let file = root.appendingPathComponent("request.json")
    try JSONSerialization.data(withJSONObject: json).write(to: file)
    let package = URL(fileURLWithPath: #filePath).deletingLastPathComponent()
      .deletingLastPathComponent().deletingLastPathComponent()
    let worker = package.appendingPathComponent("Sources/GodEyeMac/Resources/Optical/sentinel2.py")
    let destination = root.appendingPathComponent("image")
    do {
      _ = try await SarProcessor(python: URL(fileURLWithPath: python), worker: worker)
        .renderOptical(request: file, destination: destination) { _ in }
      XCTFail("Unsafe source must fail before network or raster access")
    } catch { XCTAssertTrue(error.localizedDescription.contains("trusted public archive")) }
    let failed = try SarProcessor.read(
      SentinelOpticalResult.self, from: destination.appendingPathComponent("result.json"))
    XCTAssertEqual(failed.state, .failed)
    XCTAssertFalse(
      FileManager.default.fileExists(atPath: destination.appendingPathComponent("preview.png").path)
    )
    XCTAssertTrue(
      FileManager.default.fileExists(
        atPath: destination.appendingPathComponent("diagnostics.log").path))
  }
}
