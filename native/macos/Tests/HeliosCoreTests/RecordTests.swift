import XCTest

@testable import HeliosCore

final class RecordTests: XCTestCase {
  func data(_ object: Any) throws -> Data { try JSONSerialization.data(withJSONObject: object) }
  func quake(_ id: String = "a", magnitude: Any = 3.2, coordinates: [Any] = [9, 45, 10]) -> [String:
    Any]
  {
    [
      "id": id, "geometry": ["type": "Point", "coordinates": coordinates],
      "properties": ["mag": magnitude, "place": "Test location", "time": 1_700_000_000_000],
    ]
  }
  func testNumbersNeverCoerceMissingOrBooleanToCoordinates() {
    for input: Any in [NSNull(), "", "   ", true, false, "nan", Double.infinity] {
      XCTAssertNil(Parsers.number(input))
    }
    XCTAssertEqual(Parsers.number(0), 0)
    XCTAssertEqual(Parsers.number("12.3"), 12.3)
  }
  func testEarthquakeMagnitudeThresholdAndObservedTime() throws {
    let snapshot = try Parsers.earthquakes(
      data(["features": [quake(), quake("b", magnitude: 2.4), quake("c", magnitude: NSNull())]]))
    XCTAssertEqual(snapshot.records.count, 1)
    XCTAssertEqual(snapshot.source, "USGS")
    XCTAssertEqual(snapshot.records[0].observedAt?.timeIntervalSince1970, 1_700_000_000)
    XCTAssertNil(snapshot.records[0].altitude, "Depth is not an above-ground altitude")
  }
  func testMalformedEarthquakeCannotReplacePriorSnapshot() throws {
    for coords: [Any] in [[NSNull(), 45], [true, 45], ["9", 45], [9, 91], [9, 45, "bad"]] {
      XCTAssertThrowsError(
        try Parsers.earthquakes(data(["features": [quake(coordinates: coords)]])))
    }
    XCTAssertThrowsError(try Parsers.earthquakes(data(["features": [quake(), quake()]])))
    XCTAssertThrowsError(try Parsers.earthquakes(data(["features": [quake(magnitude: true)]])))
    XCTAssertThrowsError(try Parsers.earthquakes(data(["features": [quake(magnitude: 11)]])))
  }
  func testMissingEarthquakeTimeStaysUnknown() throws {
    var row = quake()
    row["properties"] = ["mag": 3, "place": "Unknown time"]
    XCTAssertNil(try Parsers.earthquakes(data(["features": [row]])).records.first?.observedAt)
  }
  func testAdsbRetainsReportedIdentityUnitsAndFixTime() throws {
    let row: [String: Any] = [
      "hex": "A12345", "flight": " TEST123 ", "lat": 45.0, "lon": 9.0, "alt_geom": 10_000,
      "alt_baro": 9_500, "gs": 200, "seen_pos": 4, "t": "A320",
    ]
    let result = try Parsers.adsb(data(["now": 1_700_000_000_000, "ac": [row]]), feed: .military)
    XCTAssertEqual(result.records.first?.id, "military:a12345")
    XCTAssertEqual(result.records.first?.label, "TEST123")
    XCTAssertEqual(result.records.first?.altitude, 3_048)
    XCTAssertEqual(result.records.first?.observedAt?.timeIntervalSince1970, 1_699_999_996)
    XCTAssertEqual(result.records.first?.details["Type"], "A320")
  }
  func testInvalidAdsbRowsArePartialNotInventedPositions() throws {
    let row: [String: Any] = ["hex": "a", "lat": 0, "lon": 0]
    let result = try Parsers.adsb(
      data([
        "ac": [
          row, row, ["hex": "b", "lat": NSNull(), "lon": 0], ["hex": "c", "lat": 100, "lon": 0],
        ]
      ]), feed: .flights)
    XCTAssertEqual(result.records.count, 1)
    XCTAssertEqual(result.rejected, 3)
    XCTAssertNil(result.records.first?.observedAt)
    XCTAssertNil(result.records.first?.altitude)
    XCTAssertEqual(result.records.first?.coordinate, Coordinate(latitude: 0, longitude: 0))
  }
  func testEmptyAndMalformedAdsbResponsesRemainDistinct() throws {
    XCTAssertTrue(try Parsers.adsb(data(["ac": []]), feed: .flights).records.isEmpty)
    XCTAssertThrowsError(try Parsers.adsb(data(["error": "unavailable"]), feed: .flights))
  }
  func testOpenSkyUsesPositionTimeAndGeometricAltitude() throws {
    let row: [Any] = [
      "a1", " REAL123 ", "Italy", 1_700_000_000, 1_700_000_005, 9.0, 45.0, 3_000, false, 100, 0, 0,
      NSNull(), 3_200,
    ]
    let result = try Parsers.openSky(data(["time": 1_700_000_010, "states": [row]]))
    XCTAssertEqual(result.records.first?.label, "REAL123")
    XCTAssertEqual(result.records.first?.altitude, 3_200)
    XCTAssertEqual(result.records.first?.observedAt?.timeIntervalSince1970, 1_700_000_000)
    XCTAssertTrue(try Parsers.openSky(data(["states": NSNull()])).records.isEmpty)
    XCTAssertThrowsError(try Parsers.openSky(data(["error": "quota"])))
  }
  func testOpenSkyRejectsDuplicateMissingAndBooleanPositions() throws {
    let good: [Any] = [
      "a1", NSNull(), NSNull(), NSNull(), NSNull(), 0, 0, NSNull(), false, NSNull(), NSNull(),
      NSNull(), NSNull(), NSNull(),
    ]
    var bad = good
    bad[0] = "b"
    bad[5] = true
    let result = try Parsers.openSky(data(["states": [good, good, bad, ["short"]]]))
    XCTAssertEqual(result.records.count, 1)
    XCTAssertEqual(result.rejected, 3)
    XCTAssertNil(result.records.first?.observedAt)
  }
  func testTransportRejectsInsecureURLsBeforeRequests() async {
    do {
      _ = try await Transport().data(URL(string: "http://example.invalid/")!)
      XCTFail("Expected rejection")
    } catch { XCTAssertTrue(error is FeedError) }
  }
}
