import Foundation
import XCTest

@testable import GodEyeCore

private final class CatalogueFixtureProtocol: URLProtocol, @unchecked Sendable {
  struct Reply: Sendable {
    let status: Int
    let body: Data
    var length: Int? = nil
    var chunks = false
  }
  private static let lock = NSLock()
  nonisolated(unsafe) private static var replies: [Reply] = []
  nonisolated(unsafe) private static var requests: [URLRequest] = []
  static func set(_ replies: [Reply]) {
    lock.withLock {
      Self.replies = replies
      requests = []
    }
  }
  static var captured: [URLRequest] { lock.withLock { requests } }
  override class func canInit(with request: URLRequest) -> Bool { true }
  override class func canonicalRequest(for request: URLRequest) -> URLRequest { request }
  override func startLoading() {
    let reply = Self.lock.withLock {
      Self.requests.append(request)
      return Self.replies.isEmpty ? Reply(status: 500, body: Data()) : Self.replies.removeFirst()
    }
    let headers = reply.length.map { ["Content-Length": String($0)] } ?? [:]
    client?.urlProtocol(
      self,
      didReceive: HTTPURLResponse(
        url: request.url!, statusCode: reply.status, httpVersion: "HTTP/1.1", headerFields: headers)!,
      cacheStoragePolicy: .notAllowed)
    if reply.chunks {
      for offset in stride(from: 0, to: reply.body.count, by: 32) {
        client?.urlProtocol(
          self, didLoad: reply.body.subdata(in: offset..<min(reply.body.count, offset + 32)))
      }
    } else {
      client?.urlProtocol(self, didLoad: reply.body)
    }
    client?.urlProtocolDidFinishLoading(self)
  }
  override func stopLoading() {}
}
private final class DownloadCancelBox: @unchecked Sendable {
  private let lock = NSLock()
  private var download: SentinelDownload?
  func set(_ download: SentinelDownload) { lock.withLock { self.download = download } }
  func cancel() { lock.withLock { download?.cancel() } }
}
final class SentinelCatalogTests: XCTestCase {
  private let sceneName = "S1A_IW_GRDH_1SDV_20260820T170616_20260820T170641_004358_0080C1_5973.SAFE"
  private let zip = Data(
    base64Encoded:
      "UEsDBBQAAAAIAJOJRF1f9CIGOQAAADgAAAALAAAAZml4dHVyZS50eHRzT81LLUosSU1RSMkvz8vJT0xRKClKzCtOSy1SSMusKCktSrVWyMsvUSgGKsrJySxJVUhJLEnUAwBQSwECFAMUAAAACACTiURdX/QiBjkAAAA4AAAACwAAAAAAAAAAAAAAgAEAAAAAZml4dHVyZS50eHRQSwUGAAAAAAEAAQA5AAAAYgAAAAAA"
  )!
  func config() -> URLSessionConfiguration {
    let config = URLSessionConfiguration.ephemeral
    config.protocolClasses = [CatalogueFixtureProtocol.self]
    return config
  }
  func transport() -> Transport { Transport(session: URLSession(configuration: config())) }
  func region() throws -> SentinelRegion {
    try SentinelRegion(west: 9, south: 45, east: 10, north: 46)
  }
  func query() throws -> SentinelQuery {
    let start = ISO8601DateFormatter().date(from: "2026-08-01T12:00:00Z")!
    let end = ISO8601DateFormatter().date(from: "2026-08-31T12:00:00Z")!
    return try SentinelQuery(region: region(), start: start, end: end, polarization: .both)
  }
  func row(name: String? = nil, id: UUID = UUID()) -> [String: Any] {
    [
      "Id": id.uuidString, "Name": name ?? sceneName, "Online": true, "ContentLength": 12345,
      "ContentDate": ["Start": "2026-08-20T17:06:16.945658Z"],
      "GeoFootprint": [
        "type": "Polygon", "coordinates": [[[9, 45], [10, 45], [10, 46], [9, 46], [9, 45]]],
      ],
      "Attributes": [
        ["Name": "orbitDirection", "Value": "ASCENDING"],
        ["Name": "relativeOrbitNumber", "Value": 117],
      ],
    ]
  }
  func scene() throws -> SentinelScene {
    let data = try JSONSerialization.data(withJSONObject: ["value": [row()]])
    return try XCTUnwrap(SentinelPage.parse(data).scenes.first)
  }
  func temporary() throws -> URL {
    let directory = FileManager.default.temporaryDirectory.appendingPathComponent(
      "godeye-download-test-\(UUID())")
    try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
    addTeardownBlock { try? FileManager.default.removeItem(at: directory) }
    return directory
  }
  func testOfficialArchiveNamesAndLegacyMigrationPreserveBytesAndIdentity() throws {
    let scene = try scene()
    XCTAssertEqual(scene.archiveFilename, String(scene.name.dropLast(5)) + ".zip")
    XCTAssertTrue(scene.archiveFilename.contains("20260820"))
    let directory = try temporary().appendingPathComponent(scene.id.uuidString)
    try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
    let legacy = directory.appendingPathComponent("product.zip")
    let bytes = Data("test-only archive bytes".utf8)
    try bytes.write(to: legacy)
    let record = SentinelDownloadRecord(scene: scene, query: try query())
    XCTAssertEqual(try record.archive(in: directory, migrateLegacy: false), legacy)
    let renamed = try XCTUnwrap(record.archive(in: directory))
    XCTAssertEqual(renamed.lastPathComponent, scene.archiveFilename)
    XCTAssertEqual(try Data(contentsOf: renamed), bytes)
    XCTAssertFalse(FileManager.default.fileExists(atPath: legacy.path))
    XCTAssertEqual(try record.archive(in: directory), renamed)
    // A collision preserves both existing archives rather than replacing either.
    try Data("other archive".utf8).write(to: legacy)
    XCTAssertEqual(try record.archive(in: directory), renamed)
    XCTAssertEqual(try Data(contentsOf: legacy), Data("other archive".utf8))
    XCTAssertEqual(try Data(contentsOf: renamed), bytes)
  }
  func testArchiveMigrationRejectsSymlinksAndMismatchedDirectories() throws {
    let record = SentinelDownloadRecord(scene: try scene(), query: try query())
    let root = try temporary()
    let directory = root.appendingPathComponent(record.id.uuidString)
    try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
    let outside = root.appendingPathComponent("outside.zip")
    try Data("external".utf8).write(to: outside)
    try FileManager.default.createSymbolicLink(
      at: directory.appendingPathComponent("product.zip"), withDestinationURL: outside)
    XCTAssertNil(try record.archive(in: directory))
    XCTAssertNil(try record.archive(in: root))
    XCTAssertEqual(try Data(contentsOf: outside), Data("external".utf8))
  }
  func testRegionNormalizesOppositeCornersAndClosesOutline() throws {
    let a = Coordinate(latitude: 46, longitude: 10)!
    let b = Coordinate(latitude: 45, longitude: 9)!
    let rectangle = try SentinelRegion(corners: a, b)
    XCTAssertEqual(rectangle, try region())
    XCTAssertEqual(rectangle.outline.first, rectangle.outline.last)
    XCTAssertTrue(rectangle.wkt.contains("9.0 45.0"))  // longitude before latitude
  }
  func testRegionRejectsDegeneratePolarDatelineAndUnboundedRequests() {
    for bounds in [
      [9, 45, 9, 46], [170, 45, -170, 46], [9, 84, 10, 89], [9, 45, 30, 46], [.nan, 45, 10, 46],
    ] {
      XCTAssertThrowsError(
        try SentinelRegion(west: bounds[0], south: bounds[1], east: bounds[2], north: bounds[3]))
    }
  }
  func testPersistedRegionCannotBypassValidation() {
    XCTAssertThrowsError(
      try JSONDecoder().decode(
        SentinelRegion.self,
        from: Data("{\"west\":170,\"south\":45,\"east\":-170,\"north\":46}".utf8)))
  }
  func testCatalogueQueryUsesUTCInclusiveDatesAndGRDFilters() throws {
    let url = try query().url(skip: 20)
    let items = URLComponents(url: url, resolvingAgainstBaseURL: false)!.queryItems!
    let filter = items.first { $0.name == "$filter" }!.value!
    XCTAssertTrue(filter.contains("ContentDate/Start ge 2026-08-01T00:00:00Z"))
    XCTAssertTrue(filter.contains("ContentDate/Start lt 2026-09-01T00:00:00Z"))
    XCTAssertTrue(filter.contains("_IW_GRD"))
    XCTAssertTrue(filter.contains("_1SDV_"))
    XCTAssertTrue(filter.contains("not contains(Name,'_COG')"))
    XCTAssertTrue(filter.contains(try region().wkt))
    XCTAssertFalse(url.absoluteString.contains("password"))
    XCTAssertEqual(items.first { $0.name == "$skip" }?.value, "20")
    XCTAssertThrowsError(try query().url(skip: 1000))
  }
  func testStripmapBeamNamesAndSearchFilters() throws {
    let q = try query()
    let stripmap = try SentinelQuery(
      region: q.region, start: q.start, end: q.end, polarization: .both, mode: "SM")
    let filter = URLComponents(url: try stripmap.url(), resolvingAgainstBaseURL: false)!.queryItems!
      .first { $0.name == "$filter" }!.value!
    XCTAssertTrue(filter.contains("_S1_GRD"))
    XCTAssertTrue(filter.contains("_S6_GRD"))
    let data = try JSONSerialization.data(withJSONObject: [
      "value": [row(name: sceneName.replacingOccurrences(of: "_IW_", with: "_S6_"))]
    ])
    let page = try SentinelPage.parse(data)
    XCTAssertEqual(page.scenes.first?.mode, "SM")
  }
  func testUnsupportedModeAndDateRangeCannotEnterQuery() throws {
    let date = Date()
    XCTAssertThrowsError(
      try SentinelQuery(
        region: region(), start: date, end: date, polarization: .both, mode: "IW' or true"))
    XCTAssertThrowsError(
      try SentinelQuery(
        region: region(), start: date, end: date.addingTimeInterval(-1), polarization: .both))
    XCTAssertThrowsError(
      try SentinelQuery(
        region: region(), start: date, end: date.addingTimeInterval(400 * 86400),
        polarization: .both))
  }
  func testSceneMetadataAndPolarizationParsing() throws {
    let scene = try scene()
    XCTAssertEqual(scene.satellite, "S1A")
    XCTAssertEqual(scene.polarizations, "VV + VH")
    XCTAssertEqual(scene.relativeOrbit, 117)
    XCTAssertEqual(scene.orbit, "ASCENDING")
    XCTAssertEqual(scene.footprint.first?.count, 5)
    XCTAssertTrue(scene.downloadURL.absoluteString.hasSuffix("/$value"))
    XCTAssertEqual(scene.downloadURL.host, "download.dataspace.copernicus.eu")
    let vv = row(name: sceneName.replacingOccurrences(of: "_1SDV_", with: "_1SSV_"))
    let page = try SentinelPage.parse(JSONSerialization.data(withJSONObject: ["value": [vv]]))
    XCTAssertEqual(page.scenes.first?.polarizations, "VV")
  }
  func testCatalogueRejectsUnsupportedUnsafeAndDuplicateScenes() throws {
    let id = UUID()
    let data = try JSONSerialization.data(withJSONObject: [
      "value": [
        row(id: id), row(id: id), row(name: "../../escape"),
        row(name: sceneName.replacingOccurrences(of: "GRDH", with: "SLC_")),
        row(name: sceneName.replacingOccurrences(of: "1SDV", with: "1SDH")),
      ]
    ])
    let page = try SentinelPage.parse(data)
    XCTAssertEqual(page.scenes.count, 1)
    XCTAssertEqual(page.rejected, 4)
  }
  func testCatalogueRejectsInvalidAcquisitionDatesAndCOGVariants() throws {
    var badDate = row()
    badDate["ContentDate"] = ["Start": "not an acquisition date"]
    let cog = row(name: sceneName.replacingOccurrences(of: ".SAFE", with: "_COG.SAFE"))
    let page = try SentinelPage.parse(
      JSONSerialization.data(withJSONObject: ["value": [badDate, cog]]))
    XCTAssertTrue(page.scenes.isEmpty)
    XCTAssertEqual(page.rejected, 2)
  }
  func testBoundedCatalogueSearchDoesNotAuthenticate() async throws {
    CatalogueFixtureProtocol.set([
      .init(status: 200, body: try JSONSerialization.data(withJSONObject: ["value": [row()]]))
    ])
    let page = try await SentinelCatalog(transport: transport()).search(query())
    XCTAssertEqual(page.scenes.count, 1)
    XCTAssertNil(
      CatalogueFixtureProtocol.captured.first?.value(forHTTPHeaderField: "Authorization"))
  }
  func testLoginFormEscapesPasswordAndIncludesOptionalTOTP() async throws {
    CatalogueFixtureProtocol.set([
      .init(
        status: 200,
        body: Data(
          "{\"access_token\":\"fixture-access\",\"refresh_token\":\"fixture-refresh\",\"expires_in\":300}"
            .utf8))
    ])
    let account = CopernicusAccount(transport: transport())
    try await account.signIn(username: "user+test@example.com", password: "a&b+c=é", totp: "123456")
    let token = try await account.accessToken()
    XCTAssertEqual(token, "fixture-access")
    let request = try XCTUnwrap(CatalogueFixtureProtocol.captured.first)
    XCTAssertEqual(request.httpMethod, "POST")
    // URLSession may provide a body stream instead of httpBody to a URLProtocol.
    var body = request.httpBody
    if body == nil, let stream = request.httpBodyStream {
      stream.open()
      defer { stream.close() }
      var bytes = [UInt8](repeating: 0, count: 4096)
      let count = stream.read(&bytes, maxLength: bytes.count)
      if count > 0 { body = Data(bytes.prefix(count)) }
    }
    let form = String(data: try XCTUnwrap(body), encoding: .utf8)!
    XCTAssertTrue(form.contains("totp=123456"))
    XCTAssertTrue(form.contains("password=a%26b%2Bc%3D"))
    await account.signOut()
    do {
      _ = try await account.accessToken()
      XCTFail("Signed-out session retained access")
    } catch SentinelError.signedOut {} catch { XCTFail("Unexpected error: \(error)") }
  }
  func testExpiredSessionRefreshesWithoutPassword() async throws {
    CatalogueFixtureProtocol.set([
      .init(
        status: 200,
        body: Data("{\"access_token\":\"old\",\"refresh_token\":\"refresh\",\"expires_in\":1}".utf8)
      ),
      .init(
        status: 200,
        body: Data(
          "{\"access_token\":\"new\",\"refresh_token\":\"refresh\",\"expires_in\":300}".utf8)),
    ])
    let account = CopernicusAccount(transport: transport())
    try await account.signIn(username: "fixture", password: "fixture-secret")
    let token = try await account.accessToken()
    XCTAssertEqual(token, "new")
    XCTAssertEqual(CatalogueFixtureProtocol.captured.count, 2)
  }
  func testLoginFailureDoesNotExposeServerBodyOrCredentials() async {
    CatalogueFixtureProtocol.set([
      .init(status: 401, body: Data("secret server diagnostic fixture-secret".utf8))
    ])
    do {
      try await CopernicusAccount(transport: transport()).signIn(
        username: "fixture", password: "fixture-secret")
      XCTFail("Expected rejection")
    } catch {
      XCTAssertFalse(error.localizedDescription.contains("fixture-secret"))
      XCTAssertFalse(error.localizedDescription.contains("server diagnostic"))
    }
  }
  func testDownloadStreamsZipAndRemovesPartialFile() async throws {
    CatalogueFixtureProtocol.set([.init(status: 200, body: zip, length: zip.count, chunks: true)])
    let directory = try temporary()
    let destination = directory.appendingPathComponent("product.zip")
    let download = SentinelDownload(destination: destination) { _ in }
    let file = try await download.run(
      scene: scene(), token: "fixture-access", configuration: config())
    XCTAssertEqual(try Data(contentsOf: file), zip)
    XCTAssertEqual(
      try FileManager.default.contentsOfDirectory(atPath: directory.path), ["product.zip"])
    XCTAssertEqual(
      CatalogueFixtureProtocol.captured.first?.value(forHTTPHeaderField: "Authorization"),
      "Bearer fixture-access")
  }
  func testUnknownLengthDownloadIsCappedAndCleaned() async throws {
    CatalogueFixtureProtocol.set([.init(status: 200, body: zip, chunks: true)])
    let directory = try temporary()
    let destination = directory.appendingPathComponent("product.zip")
    do {
      _ = try await SentinelDownload(destination: destination, maximumBytes: 64) { _ in }.run(
        scene: scene(), token: "fixture", configuration: config())
      XCTFail("Expected size cap")
    } catch SentinelError.oversized {} catch { XCTFail("Unexpected error: \(error)") }
    XCTAssertTrue(try FileManager.default.contentsOfDirectory(atPath: directory.path).isEmpty)
  }
  func testKnownLengthOversizedDownloadIsRejectedAndCleaned() async throws {
    CatalogueFixtureProtocol.set([.init(status: 200, body: zip, length: zip.count)])
    let directory = try temporary()
    do {
      _ = try await SentinelDownload(
        destination: directory.appendingPathComponent("product.zip"), maximumBytes: 64
      ) { _ in }.run(scene: scene(), token: "fixture", configuration: config())
      XCTFail("Expected size cap")
    } catch SentinelError.oversized {} catch { XCTFail("Unexpected error: \(error)") }
    XCTAssertTrue(try FileManager.default.contentsOfDirectory(atPath: directory.path).isEmpty)
  }
  func testTruncatedZipAndHTTPFailureLeaveNoPublishedDataset() async throws {
    for reply in [
      CatalogueFixtureProtocol.Reply(status: 200, body: zip.prefix(50), length: 50),
      .init(status: 401, body: Data("unauthorized".utf8)),
      .init(status: 200, body: Data("<html>not data</html>".utf8)),
    ] {
      CatalogueFixtureProtocol.set([reply])
      let directory = try temporary()
      do {
        _ = try await SentinelDownload(destination: directory.appendingPathComponent("product.zip"))
        { _ in }.run(scene: scene(), token: "fixture", configuration: config())
        XCTFail("Expected failed download")
      } catch {}
      XCTAssertTrue(try FileManager.default.contentsOfDirectory(atPath: directory.path).isEmpty)
    }
  }
  func testCancellationBeforeStartLeavesNoPartialFiles() async throws {
    let directory = try temporary()
    let download = SentinelDownload(destination: directory.appendingPathComponent("product.zip")) {
      _ in
    }
    download.cancel()
    CatalogueFixtureProtocol.set([.init(status: 200, body: zip)])
    do {
      _ = try await download.run(scene: scene(), token: "fixture", configuration: config())
      XCTFail("Cancellation published a dataset")
    } catch {}
    XCTAssertTrue(try FileManager.default.contentsOfDirectory(atPath: directory.path).isEmpty)
  }
  func testIncorrectResponseLengthLeavesNoPublishedDataset() async throws {
    CatalogueFixtureProtocol.set([.init(status: 200, body: zip, length: zip.count + 100)])
    let directory = try temporary()
    do {
      _ = try await SentinelDownload(destination: directory.appendingPathComponent("product.zip")) {
        _ in
      }
      .run(scene: scene(), token: "fixture", configuration: config())
      XCTFail("Incomplete response was published")
    } catch {}
    XCTAssertTrue(try FileManager.default.contentsOfDirectory(atPath: directory.path).isEmpty)
  }
  func testCancellationDuringTransferCleansPartialData() async throws {
    let directory = try temporary()
    let cancellation = DownloadCancelBox()
    let download = SentinelDownload(destination: directory.appendingPathComponent("product.zip")) {
      _ in
      cancellation.cancel()
    }
    cancellation.set(download)
    CatalogueFixtureProtocol.set([.init(status: 200, body: zip, length: zip.count, chunks: true)])
    do {
      _ = try await download.run(scene: scene(), token: "fixture", configuration: config())
      XCTFail("Cancelled data was published")
    } catch {}
    XCTAssertTrue(try FileManager.default.contentsOfDirectory(atPath: directory.path).isEmpty)
  }
  func testExistingCompletedDownloadCannotBeOverwritten() async throws {
    let directory = try temporary()
    let file = directory.appendingPathComponent("product.zip")
    try zip.write(to: file)
    do {
      _ = try await SentinelDownload(destination: file) { _ in }.run(
        scene: scene(), token: "fixture", configuration: config())
      XCTFail("Overwrote prior dataset")
    } catch {}
    XCTAssertEqual(try Data(contentsOf: file), zip)
  }
  func testAuthorizationRedirectPolicyUsesExactHTTPSHosts() {
    XCTAssertTrue(
      SentinelDownload.trusted(URL(string: "https://download.dataspace.copernicus.eu/path")))
    for url in [
      "https://download.dataspace.copernicus.eu.evil.test/a",
      "http://download.dataspace.copernicus.eu/a", "https://evil.test/a",
      "https://user@download.dataspace.copernicus.eu/a",
      "https://download.dataspace.copernicus.eu:8080/a",
    ] {
      XCTAssertFalse(SentinelDownload.trusted(URL(string: url)))
    }
  }
  func testDownloadProvenanceRoundTripContainsNoSecrets() throws {
    let record = try SentinelDownloadRecord(scene: scene(), query: query())
    let data = try JSONEncoder().encode(record)
    let decoded = try JSONDecoder().decode(SentinelDownloadRecord.self, from: data)
    XCTAssertEqual(decoded.scene, record.scene)
    XCTAssertEqual(decoded.query, record.query)
    let json = String(data: data, encoding: .utf8)!
    XCTAssertFalse(json.contains("password"))
    XCTAssertFalse(json.contains("access_token"))
    XCTAssertFalse(json.contains("Authorization"))
  }
  func testLiveCopernicusCatalogue() async throws {
    guard ProcessInfo.processInfo.environment["GODEYE_LIVE_CHECKS"] == "1" else {
      throw XCTSkip("Live catalogue check is opt-in")
    }
    let page = try await SentinelCatalog().search(query())
    XCTAssertFalse(page.scenes.isEmpty)
    XCTAssertTrue(page.scenes.allSatisfy { $0.mode == "IW" && $0.polarizations == "VV + VH" })
  }
}
