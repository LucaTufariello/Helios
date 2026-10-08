import Foundation
import XCTest

@testable import GodEyeCore

private final class FixtureProtocol: URLProtocol, @unchecked Sendable {
  struct Fixture: Sendable {
    let status: Int
    let body: Data
    var omitLength = false
  }
  private static let lock = NSLock()
  nonisolated(unsafe) private static var fixture = Fixture(status: 200, body: Data())
  static func set(_ value: Fixture) { lock.withLock { fixture = value } }
  override class func canInit(with request: URLRequest) -> Bool { true }
  override class func canonicalRequest(for request: URLRequest) -> URLRequest { request }
  override func startLoading() {
    let value = Self.lock.withLock { Self.fixture }
    let response = HTTPURLResponse(
      url: request.url!, statusCode: value.status, httpVersion: "HTTP/1.1",
      headerFields: value.omitLength ? [:] : ["Content-Length": String(value.body.count)])!
    client?.urlProtocol(self, didReceive: response, cacheStoragePolicy: .notAllowed)
    client?.urlProtocol(self, didLoad: value.body)
    client?.urlProtocolDidFinishLoading(self)
  }
  override func stopLoading() {}
}
final class TransportTests: XCTestCase {
  func transport() -> Transport {
    let configuration = URLSessionConfiguration.ephemeral
    configuration.protocolClasses = [FixtureProtocol.self]
    return Transport(session: URLSession(configuration: configuration))
  }
  func testSuccessfulBoundedNativeResponse() async throws {
    FixtureProtocol.set(.init(status: 200, body: Data("actual source bytes".utf8)))
    let transport = transport()
    defer { transport.session.invalidateAndCancel() }
    let result = try await transport.data(
      URL(string: "https://fixture.invalid/source")!, maximumBytes: 100)
    XCTAssertEqual(result, Data("actual source bytes".utf8))
  }
  func testQuotaFailureDoesNotLookLikeAnEmptySuccessfulFeed() async {
    FixtureProtocol.set(.init(status: 429, body: Data("quota".utf8)))
    let transport = transport()
    defer { transport.session.invalidateAndCancel() }
    do {
      _ = try await transport.data(URL(string: "https://fixture.invalid/source")!)
      XCTFail("Expected HTTP rejection")
    } catch FeedError.http(let status) { XCTAssertEqual(status, 429) } catch {
      XCTFail("Wrong error: \(error)")
    }
  }
  func testOversizedFeedIsRejectedBeforeParsing() async {
    FixtureProtocol.set(.init(status: 200, body: Data(repeating: 65, count: 101)))
    let transport = transport()
    defer { transport.session.invalidateAndCancel() }
    do {
      _ = try await transport.data(
        URL(string: "https://fixture.invalid/source")!, maximumBytes: 100)
      XCTFail("Expected capped response")
    } catch FeedError.oversized {} catch { XCTFail("Wrong error: \(error)") }
  }
  func testUnannouncedOversizedBodyIsAlsoCapped() async {
    FixtureProtocol.set(.init(status: 200, body: Data(repeating: 65, count: 101), omitLength: true))
    let transport = transport()
    defer { transport.session.invalidateAndCancel() }
    do {
      _ = try await transport.data(
        URL(string: "https://fixture.invalid/source")!, maximumBytes: 100)
      XCTFail("Unknown content length bypassed the response cap")
    } catch FeedError.oversized {} catch { XCTFail("Wrong error: \(error)") }
  }
  func testCancelledSourceCannotPublishResult() async {
    FixtureProtocol.set(.init(status: 200, body: Data(repeating: 65, count: 10_000)))
    let transport = transport()
    defer { transport.session.invalidateAndCancel() }
    let task = Task { try await transport.data(URL(string: "https://fixture.invalid/source")!) }
    task.cancel()
    do {
      _ = try await task.value
      XCTFail("Cancelled source was published")
    } catch {}
  }
}
