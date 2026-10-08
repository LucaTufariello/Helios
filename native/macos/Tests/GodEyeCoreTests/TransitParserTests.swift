import Foundation
import XCTest

@testable import GodEyeCore

final class TransitParserTests: XCTestCase {
  private func varint(_ value: UInt64) -> [UInt8] {
    var value = value
    var bytes: [UInt8] = []
    repeat {
      let byte = UInt8(value & 127)
      value >>= 7
      bytes.append(value == 0 ? byte : byte | 128)
    } while value != 0
    return bytes
  }
  private func message(_ number: Int, _ payload: [UInt8]) -> [UInt8] {
    varint(UInt64(number * 8 + 2)) + varint(UInt64(payload.count)) + payload
  }
  private func number(_ field: Int, _ value: UInt64) -> [UInt8] {
    varint(UInt64(field * 8)) + varint(value)
  }
  private func fixed(_ field: Int, _ value: Float) -> [UInt8] {
    let bits = value.bitPattern
    return varint(UInt64(field * 8 + 5))
      + (0..<4).map { UInt8(truncatingIfNeeded: bits >> ($0 * 8)) }
  }
  private func feed(at time: Date?, latitude: Float = 0) -> Data {
    let header = message(1, Array("2.0".utf8)) + number(3, 1_700_000_000)
    let position = fixed(1, latitude) + fixed(2, 0) + fixed(5, 0)
    let trip = message(5, Array("Red".utf8))
    let descriptor =
      message(1, Array("bus-1".utf8)) + message(2, Array("Actual operator label".utf8))
    let fixTime = time.map { number(5, UInt64($0.timeIntervalSince1970)) } ?? []
    let vehicle = message(1, trip) + message(2, position) + fixTime + message(8, descriptor)
    let entity = message(1, Array("entity-1".utf8)) + message(4, vehicle)
    return Data(message(1, header) + message(2, entity))
  }
  func testReportedZeroCoordinatesSpeedAndVehicleFixTime() throws {
    let provider = try XCTUnwrap(TransitOperator.catalog.first)
    let now = Date(timeIntervalSince1970: 1_700_000_001)
    let record = try XCTUnwrap(
      TransitParser.parse(feed(at: now), provider: provider, now: now).records.first)
    XCTAssertEqual(record.coordinate, Coordinate(latitude: 0, longitude: 0))
    XCTAssertEqual(record.observedAt, now)
    XCTAssertEqual(record.details["Speed"], "0.0 km/h")
    XCTAssertEqual(record.details["Route"], "Red")
    XCTAssertEqual(record.source, provider.attribution)
  }
  func testMissingFixTimeNeverFallsBackToHeaderAndStaleFixesAreExcluded() throws {
    let provider = try XCTUnwrap(TransitOperator.catalog.first)
    let now = Date(timeIntervalSince1970: 1_700_000_001)
    let record = try XCTUnwrap(
      TransitParser.parse(feed(at: nil), provider: provider, now: now).records.first)
    XCTAssertNil(record.observedAt)
    XCTAssertTrue(
      try TransitParser.parse(feed(at: now.addingTimeInterval(-181)), provider: provider, now: now)
        .records.isEmpty)
    XCTAssertTrue(
      try TransitParser.parse(feed(at: now, latitude: .nan), provider: provider, now: now).records
        .isEmpty)
  }
  func testTruncatedWireAndExcessiveLengthAreRejected() throws {
    let provider = try XCTUnwrap(TransitOperator.catalog.first)
    for bytes: [UInt8] in [
      [0x80], [0x0a, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x7f], [0x0d, 0, 0],
    ] {
      XCTAssertThrowsError(try TransitParser.parse(Data(bytes), provider: provider))
    }
  }
}
