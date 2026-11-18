import Foundation
import XCTest

@testable import HeliosCore

final class AdditionalFeatureTests: XCTestCase {
  private let point = Coordinate(latitude: 45.4641, longitude: 9.1919)!
  private func json(_ value: Any) throws -> Data {
    try JSONSerialization.data(withJSONObject: value)
  }
  func testViewportMatchesEllipsoidAndRejectsGlobalAndDateLineQueries() throws {
    let camera = Camera(center: point, altitude: 20_000)
    let center = try XCTUnwrap(GeoBounds.surface(camera: camera, x: 0, y: 0, aspect: 1.4))
    XCTAssertEqual(center.latitude, point.latitude, accuracy: 1e-8)
    XCTAssertEqual(center.longitude, point.longitude, accuracy: 1e-8)
    let box = try GeoBounds.viewport(camera: camera, aspect: 1.4, maximumSpan: 2)
    for x in [-1.0, 0, 1] {
      for y in [-1.0, 0, 1] {
        XCTAssertTrue(
          box.contains(try XCTUnwrap(GeoBounds.surface(camera: camera, x: x, y: y, aspect: 1.4))))
      }
    }
    XCTAssertThrowsError(
      try GeoBounds.viewport(
        camera: Camera(center: point, altitude: 20_000_000), aspect: 1, maximumSpan: 10))
    XCTAssertThrowsError(
      try GeoBounds.viewport(
        camera: Camera(center: Coordinate(latitude: 0, longitude: 179.999)!, altitude: 20_000),
        aspect: 1, maximumSpan: 10))
    XCTAssertThrowsError(try GeoBounds.viewport(camera: camera, aspect: .nan, maximumSpan: 2))
    XCTAssertNil(GeoBounds(south: 10, west: 10, north: 0, east: 20))
  }
  func testMappedSiteQueriesStayNarrowAndBounded() throws {
    let box = GeoBounds(south: 45.4, west: 9, north: 45.5, east: 9.2)!
    let installations = try MappedSites.query(feed: .installations, bounds: box)
    for kind in ["airfield", "naval_base", "range", "barracks", "base"] {
      XCTAssertTrue(installations.contains("[\"military\"=\"\(kind)\"]"))
    }
    XCTAssertTrue(installations.contains("out center tags geom 700"))
    let energy = try MappedSites.query(feed: .energy, bounds: box)
    XCTAssertTrue(energy.contains("refinery"))
    XCTAssertTrue(energy.contains("geom 400"))
    XCTAssertThrowsError(
      try MappedSites.query(
        feed: .energy, bounds: GeoBounds(south: 40, west: 8, north: 45, east: 9)!))
    XCTAssertThrowsError(try MappedSites.query(feed: .flights, bounds: box))
  }
  func testMappedFootprintHasProvenanceAndNeverClaimsLiveOccupancy() throws {
    let box = GeoBounds(south: 45, west: 9, north: 46, east: 10)!
    let geometry: [[String: Any]] = [
      ["lat": 45.1, "lon": 9.1], ["lat": 45.2, "lon": 9.1], ["lat": 45.2, "lon": 9.2],
      ["lat": 45.1, "lon": 9.1],
    ]
    let data = try json([
      "elements": [
        [
          "type": "way", "id": 123, "tags": ["military": "base", "name": "Mapped site"],
          "geometry": geometry,
        ]
      ]
    ])
    let record = try XCTUnwrap(
      MappedSites.parse(data, feed: .installations, bounds: box).records.first)
    XCTAssertEqual(record.footprint.count, 4)
    XCTAssertTrue(record.details["Location method"]!.contains("midpoint"))
    XCTAssertTrue(record.details["Evidence"]!.contains("unknown"))
    XCTAssertNil(record.observedAt)
    XCTAssertEqual(record.details["Source URL"], "https://www.openstreetmap.org/way/123")
  }
  func testMalformedUnrelatedDuplicateAndOpenGeometryAreAdmittedSafely() throws {
    let box = GeoBounds(south: 0, west: 0, north: 1, east: 1)!
    let row: [String: Any] = [
      "type": "node", "id": 1, "lat": 0, "lon": 0, "tags": ["power": "plant"],
      "geometry": [["lat": 0, "lon": 0], ["lat": 0, "lon": 0.1], ["lat": 0.1, "lon": 0.1]],
    ]
    let result = try MappedSites.parse(
      json([
        "elements": [
          row, row,
          ["type": "node", "id": 2, "lat": true, "lon": 0, "tags": ["power": "substation"]],
          ["type": "node", "id": 3, "lat": 0.5, "lon": 0.5, "tags": ["shop": "yes"]],
        ]
      ]), feed: .energy, bounds: box)
    XCTAssertEqual(result.records.count, 1)
    XCTAssertEqual(result.rejected, 3)
    XCTAssertTrue(result.records[0].footprint.isEmpty)
    XCTAssertThrowsError(
      try MappedSites.parse(
        json(["elements": [], "remark": "runtime error: timeout"]), feed: .energy, bounds: box))
  }
  func testSaturatedMapResponsesExplicitlyDiscloseCoverageLimit() throws {
    let box = GeoBounds(south: 0, west: 0, north: 1, east: 1)!
    let rows = (1...400).map {
      ["type": "node", "id": $0, "lat": 0.5, "lon": 0.5, "tags": ["power": "plant"]]
        as [String: Any]
    }
    let snapshot = try MappedSites.parse(json(["elements": rows]), feed: .energy, bounds: box)
    XCTAssertEqual(snapshot.records.count, 400)
    XCTAssertTrue(snapshot.notices.contains { $0.contains("Coverage may be incomplete") })
  }
  func testSolarBoundaryIsClosedFiniteAndOnHorizonAcrossSeasons() throws {
    for date in [
      "2026-03-20T12:00:00Z", "2026-06-21T00:00:00Z", "2026-09-22T06:00:00Z",
      "2026-12-21T18:00:00Z",
    ] {
      let solar = SolarState(date: ISO8601DateFormatter().date(from: date)!)
      XCTAssertEqual(solar.terminator.count, 721)
      for coordinate in solar.terminator {
        XCTAssertEqual(solar.elevation(at: coordinate), 0, accuracy: 1e-7)
      }
      XCTAssertEqual(solar.elevation(at: solar.subsolar), 90, accuracy: 1e-6)
      XCTAssertEqual(
        solar.terminator.first!.latitude, solar.terminator.last!.latitude, accuracy: 1e-8)
      XCTAssertEqual(
        solar.terminator.first!.longitude, solar.terminator.last!.longitude, accuracy: 1e-8)
    }
  }
}
