import Foundation
import XCTest

@testable import GodEyeCore

// Explicit opt-in public-provider checks. The ordinary suite remains reproducible offline.
final class LiveMigrationTests: XCTestCase {
  private func enabled() throws {
    if ProcessInfo.processInfo.environment["GODEYE_LIVE_CHECKS"] != "1" {
      throw XCTSkip("Set GODEYE_LIVE_CHECKS=1 for real public-provider checks.")
    }
  }
  private func source(_ feed: FeedKind) async throws -> Snapshot {
    let sources = NativeSources()
    do {
      let snapshot = try await sources.snapshot(
        feed, center: Camera.initial.center, configuration: SourceConfiguration())
      await sources.shutdown()
      return snapshot
    } catch {
      await sources.shutdown()
      throw error
    }
  }
  func testRealTLECatalog() async throws {
    try enabled()
    let snapshot = try await source(.satellites)
    XCTAssertGreaterThan(snapshot.records.count, 0)
    print("Live satellites: \(snapshot.records.count)")
  }
  func testRealOperatorTransit() async throws {
    try enabled()
    let snapshot = try await source(.transit)
    XCTAssertGreaterThan(snapshot.records.count, 0)
    print("Live transit: \(snapshot.records.count)")
  }
  func testRealLondonCameras() async throws {
    try enabled()
    let snapshot = try await source(.cameras)
    XCTAssertGreaterThan(snapshot.records.count, 0)
    print("Live cameras: \(snapshot.records.count)")
  }
  func testRealFinlandCameras() async throws {
    try enabled()
    let sources = NativeSources()
    var configuration = SourceConfiguration()
    configuration.cameraProvider = .finland
    do {
      let snapshot = try await sources.snapshot(
        .cameras, center: Camera.initial.center, configuration: configuration)
      await sources.shutdown()
      XCTAssertGreaterThan(snapshot.records.count, 0)
      print("Live Finland cameras: \(snapshot.records.count)")
    } catch {
      await sources.shutdown()
      throw error
    }
  }
  func testRealSentinelCatalog() async throws {
    try enabled()
    let snapshot = try await source(.sentinel)
    XCTAssertGreaterThan(snapshot.records.count, 0)
    print("Live Sentinel products: \(snapshot.records.count)")
  }
  func testRealStreetRoute() async throws {
    try enabled()
    let transport = Transport()
    defer { transport.session.invalidateAndCancel() }
    let route = try await transport.route(
      from: Camera.initial.center, to: Coordinate(latitude: 45.4705, longitude: 9.1798)!, mode: .car
    )
    XCTAssertGreaterThan(route.distance, 0)
    XCTAssertGreaterThan(route.points.count, 1)
    XCTAssertFalse(route.steps.isEmpty)
    print("Live route: \(route.points.count) vertices, \(route.steps.count) turns")
  }
}
