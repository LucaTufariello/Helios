import Foundation
import XCTest

@testable import HeliosCore

private final class SourceProtocol: URLProtocol, @unchecked Sendable {
  private static let lock = NSLock()
  nonisolated(unsafe) private static var responder: (@Sendable (URLRequest) -> (Int, Data))?
  nonisolated(unsafe) private static var requests: [URLRequest] = []
  static func configure(_ handler: @escaping @Sendable (URLRequest) -> (Int, Data)) {
    lock.withLock {
      responder = handler
      requests = []
    }
  }
  static var log: [URLRequest] { lock.withLock { requests } }
  override class func canInit(with request: URLRequest) -> Bool { true }
  override class func canonicalRequest(for request: URLRequest) -> URLRequest { request }
  override func startLoading() {
    let handler = Self.lock.withLock {
      Self.requests.append(request)
      return Self.responder
    }
    let (status, body) = handler?(request) ?? (500, Data())
    let response = HTTPURLResponse(
      url: request.url!, statusCode: status, httpVersion: "HTTP/1.1",
      headerFields: ["Content-Length": String(body.count)])!
    client?.urlProtocol(self, didReceive: response, cacheStoragePolicy: .notAllowed)
    client?.urlProtocol(self, didLoad: body)
    client?.urlProtocolDidFinishLoading(self)
  }
  override func stopLoading() {}
}
final class AdditionalSourceTransportTests: XCTestCase {
  private func sources() -> (AdditionalSources, Transport) {
    let config = URLSessionConfiguration.ephemeral
    config.protocolClasses = [SourceProtocol.self]
    let transport = Transport(session: URLSession(configuration: config))
    return (AdditionalSources(transport: transport), transport)
  }
  func testMappedSourceFallsBackAndCacheKeepsOriginalRetrievalTime() async throws {
    SourceProtocol.configure { request in
      if request.url!.host == "overpass-api.de" { return (503, Data()) }
      return (
        200,
        Data(
          #"{"elements":[{"type":"node","id":123,"lat":45.4641,"lon":9.1919,"tags":{"power":"plant","name":"Fixture"}}]}"#
            .utf8)
      )
    }
    let (sources, transport) = sources()
    defer { transport.session.invalidateAndCancel() }
    let camera = Camera(center: Camera.initial.center, altitude: 20_000)
    let first = try await sources.snapshot(
      feed: .energy, camera: camera, aspect: 1)
    XCTAssertEqual(first.records.count, 1)
    XCTAssertEqual(
      SourceProtocol.log.map { $0.url!.host! }, ["overpass-api.de", "overpass.kumi.systems"])
    XCTAssertTrue(
      SourceProtocol.log.allSatisfy {
        $0.httpMethod == "POST"
          && $0.value(forHTTPHeaderField: "Content-Type") == "application/x-www-form-urlencoded"
      })
    let second = try await sources.snapshot(
      feed: .energy, camera: camera, aspect: 1)
    XCTAssertEqual(second.retrievedAt, first.retrievedAt)
    XCTAssertNotNil(second.retrievedAt)
    XCTAssertEqual(SourceProtocol.log.count, 2)
  }
  func testGlobalMappedSourceIsRejectedWithoutNetworkTraffic() async throws {
    SourceProtocol.configure { _ in
      XCTFail("Global query reached provider")
      return (200, Data())
    }
    let (sources, transport) = sources()
    defer { transport.session.invalidateAndCancel() }
    do {
      _ = try await sources.snapshot(feed: .energy, camera: .initial, aspect: 1)
      XCTFail("Expected local-view rejection")
    } catch FeedError.coverage {} catch { XCTFail("Wrong error: \(error)") }
    XCTAssertTrue(SourceProtocol.log.isEmpty)
  }
}
