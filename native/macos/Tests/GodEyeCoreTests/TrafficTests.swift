import Foundation
import XCTest

@testable import GodEyeCore

final class TrafficTests: XCTestCase {
  private func varint(_ value: UInt64) -> [UInt8] {
    var value = value
    var result: [UInt8] = []
    repeat {
      let byte = UInt8(value & 127)
      value >>= 7
      result.append(value == 0 ? byte : byte | 128)
    } while value != 0
    return result
  }
  private func message(_ number: Int, _ payload: [UInt8]) -> [UInt8] {
    varint(UInt64(number * 8 + 2)) + varint(UInt64(payload.count)) + payload
  }
  private func integer(_ number: Int, _ value: UInt64) -> [UInt8] {
    varint(UInt64(number * 8)) + varint(value)
  }
  private func fixture(
    ratio: Double?, closed: Bool = false, malformed: Bool = false,
    secondRoad: Bool = false, reverse: Bool = false, duplicate: Bool = false
  ) -> Data {
    let keys = message(3, Array("traffic_level".utf8)) + message(3, Array("road_closure".utf8))
    var values: [UInt8] = []
    var tags: [UInt64] = []
    if let ratio {
      values += message(
        4,
        varint(3 * 8 + 1) + (0..<8).map { UInt8(truncatingIfNeeded: ratio.bitPattern >> ($0 * 8)) })
      tags += [0, 0]
    }
    if closed {
      values += message(4, integer(7, 1))
      tags += [1, ratio == nil ? 0 : 1]
    }
    // MVT commands: move (0,0), line (+100,+100). Coordinates are delta/zigzag encoded.
    let geometry: [UInt64] = malformed ? [9, 0] : [9, 0, 0, 10, 200, 200]
    let feature =
      integer(3, 2) + message(2, tags.flatMap(varint)) + message(4, geometry.flatMap(varint))
    let other =
      integer(3, 2) + message(2, tags.flatMap(varint))
      + message(4, [UInt64(9), 2, 2, 10, 200, 200].flatMap(varint))
    let features = secondRoad ? (reverse ? [other, feature] : [feature, other]) : [feature]
    let layer =
      message(1, Array("Traffic flow".utf8)) + integer(15, 2) + integer(5, 4096) + keys + values
      + features.flatMap { message(2, $0) } + (duplicate ? message(2, feature) : [])
    return Data(message(3, layer))
  }
  func testReportedZeroFlowAndGeometryDoNotInventAcquisitionTime() throws {
    let tile = try XCTUnwrap(TileID(x: 2048, y: 2048, zoom: 12))
    let time = Date(timeIntervalSince1970: 1_700_000_000)
    let snapshot = try TrafficParser.parse(fixture(ratio: 0), tile: tile, retrievedAt: time)
    let record = try XCTUnwrap(snapshot.records.first)
    XCTAssertEqual(record.coordinate, Coordinate(latitude: 0, longitude: 0))
    XCTAssertEqual(record.paths.first?.count, 2)
    XCTAssertEqual(record.details["Relative flow"], "0.0")
    XCTAssertNil(record.observedAt)
    XCTAssertEqual(snapshot.retrievedAt, time)
  }
  func testMissingOrInvalidFlowIsOmittedExceptExplicitClosure() throws {
    let tile = try XCTUnwrap(TileID(x: 2048, y: 2048, zoom: 12))
    for value: Double? in [nil, .nan, -0.1, 1.1] {
      XCTAssertTrue(try TrafficParser.parse(fixture(ratio: value), tile: tile).records.isEmpty)
    }
    let closure = try XCTUnwrap(
      TrafficParser.parse(fixture(ratio: nil, closed: true), tile: tile).records.first)
    XCTAssertEqual(closure.details["Road closed"], "Yes")
    XCTAssertNil(closure.details["Relative flow"])
  }
  func testMalformedGeometryAndWireCannotBecomeRoads() throws {
    let tile = try XCTUnwrap(TileID(x: 2048, y: 2048, zoom: 12))
    let snapshot = try TrafficParser.parse(fixture(ratio: 0.7, malformed: true), tile: tile)
    XCTAssertEqual(snapshot.rejected, 1)
    XCTAssertTrue(snapshot.records.isEmpty)
    XCTAssertThrowsError(try TrafficParser.parse(Data([0x1a, 0xff]), tile: tile))
  }
  func testTrafficRefusesMissingCredentialsBeforeNetworkRequests() async throws {
    let source = TrafficSource()
    do {
      _ = try await source.snapshot(camera: Camera.initial, aspect: 1, key: "")
      XCTFail("A missing credential must not become an empty successful feed")
    } catch { XCTAssertTrue(error.localizedDescription.contains("TomTom")) }
    await source.shutdown()
  }
  func testRoadSelectionSurvivesFeatureReorderingAndFlowRefresh() throws {
    let tile = try XCTUnwrap(TileID(x: 2048, y: 2048, zoom: 12))
    let before = try TrafficParser.parse(fixture(ratio: 0.7, secondRoad: true), tile: tile)
    let after = try TrafficParser.parse(
      fixture(ratio: 0.3, secondRoad: true, reverse: true), tile: tile)
    XCTAssertEqual(before.records.count, 2)
    XCTAssertEqual(after.records.count, 2)
    for record in before.records {
      let refreshed = try XCTUnwrap(after.records.first { $0.id == record.id })
      XCTAssertEqual(record.coordinate, refreshed.coordinate)
      XCTAssertEqual(refreshed.details["Relative flow"], "0.3")
    }
    let duplicates = try TrafficParser.parse(fixture(ratio: 0.7, duplicate: true), tile: tile)
    XCTAssertEqual(duplicates.records.count, 1)
    XCTAssertEqual(duplicates.rejected, 1)
  }
}
