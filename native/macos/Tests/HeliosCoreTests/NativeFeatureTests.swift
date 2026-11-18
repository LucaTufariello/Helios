import Foundation
import XCTest

@testable import HeliosCore

final class NativeFeatureTests: XCTestCase {
  private func json(_ value: Any) throws -> Data {
    try JSONSerialization.data(withJSONObject: value)
  }
  func testElevatedObjectsRemainVisibleBeyondSurfaceHorizon() throws {
    let eye = try XCTUnwrap(Coordinate(latitude: 0, longitude: 0)).ecef(altitude: 100_000)
    let near = try XCTUnwrap(Coordinate(latitude: 0, longitude: 20))
    XCTAssertFalse(GlobeVisibility.visible(from: eye, to: near.ecef(altitude: 50)))
    XCTAssertTrue(GlobeVisibility.visible(from: eye, to: near.ecef(altitude: 1_000_000)))
    let far = try XCTUnwrap(Coordinate(latitude: 0, longitude: 180))
    XCTAssertFalse(GlobeVisibility.visible(from: eye, to: far.ecef(altitude: 1_000_000)))
  }
  func testSGP4MatchesValladoReferenceAtEpoch() throws {
    let tle = """
      VANGUARD 1
      1 00005U 58002B   00179.78495062  .00000023  00000-0  28098-4 0  4753
      2 00005  34.2682 348.7242 1859667 331.7664  19.3264 10.82419157413667
      """
    let catalog = try OrbitalCatalog.parse(Data(tle.utf8))
    let position = try catalog.satellites[0].position(minsAfterEpoch: 0)
    XCTAssertEqual(position.x, 7022.46529266, accuracy: 0.03)
    XCTAssertEqual(position.y, -1400.08296755, accuracy: 0.03)
    XCTAssertEqual(position.z, 0.03995155, accuracy: 0.03)
    XCTAssertThrowsError(try OrbitalCatalog.parse(Data("Not a TLE catalog".utf8)))
    let record = try XCTUnwrap(
      catalog.snapshot(at: Date(timeIntervalSince1970: 962131819.733568)).records.first)
    XCTAssertEqual(
      record.details["Position basis"], "SGP4/SDP4 propagated estimate, not a live observation")
    XCTAssertNotNil(record.observedAt)
    let satellite = catalog.satellites[0]
    let orbit = try satellite.estimatedOrbit(at: satellite.epoch)
    XCTAssertEqual(orbit.points.count, 97)
    XCTAssertEqual(orbit.altitudes.count, 97)
    XCTAssertTrue(orbit.altitudes.allSatisfy { $0 > 0 && $0.isFinite })
    XCTAssertEqual(orbit.kind, .orbit)
  }
  func testPropagationMatchesLegacyEngineAcrossNearAndDeepOrbits() throws {
    struct Fixture: Decodable {
      let tle: String
      let minutes: Double
      let position: [Double]
    }
    let url = try XCTUnwrap(
      Bundle.module.url(
        forResource: "sgp4-reference", withExtension: "json", subdirectory: "Fixtures"))
    let fixtures = try JSONDecoder().decode([Fixture].self, from: Data(contentsOf: url))
    for fixture in fixtures {
      let catalog = try OrbitalCatalog.parse(Data(fixture.tle.utf8))
      let result = try catalog.satellites[0].position(minsAfterEpoch: fixture.minutes)
      for (actual, expected) in zip([result.x, result.y, result.z], fixture.position) {
        XCTAssertEqual(
          actual, expected, accuracy: 0.1,
          "t=\(fixture.minutes) \(catalog.satellites[0].commonName)")
      }
    }
  }
  func testBundledLayersRetainCountsProvenanceAndNoFakeTimes() throws {
    for (feed, count) in [
      (FeedKind.datacenters, 4351), (.dams, 704), (.cables, 712 + 1917),
    ] {
      let snapshot = try ReferenceLayers.load(feed)
      XCTAssertEqual(snapshot.records.count + snapshot.rejected, count, feed.rawValue)
      XCTAssertTrue(snapshot.records.allSatisfy { $0.observedAt == nil && $0.altitude == nil })
      XCTAssertTrue(
        snapshot.records.allSatisfy { $0.details["Evidence"]?.contains("Bundled") == true })
    }
    XCTAssertTrue(
      try ReferenceLayers.load(.cables).records.allSatisfy {
        $0.details["Data license"] == "CC BY-NC-SA 3.0 (non-commercial)"
      })
    XCTAssertFalse(TransitOperator.catalog.contains { $0.id == "atac-rome" })
  }
  func testFirmsPreservesZeroAndCategoricalConfidenceAndFiltersUTC() throws {
    let text = """
      latitude,longitude,acq_date,acq_time,confidence,frp,satellite,instrument
      0,0,2026-10-01,45,n,0,N,VIIRS
      true,0,2026-10-01,45,h,2,N,VIIRS
      0,0,2026-09-01,1230,h,2,N,VIIRS
      0,0,2026-10-01,2599,h,2,N,VIIRS
      """
    let time = ProviderParsers.date("2026-10-01T12:00:00Z")!
    let snapshot = try ProviderParsers.fires(Data(text.utf8), now: time)
    XCTAssertEqual(snapshot.records.count, 1)
    XCTAssertEqual(snapshot.records[0].observedAt, ProviderParsers.date("2026-10-01T00:45:00Z"))
    XCTAssertEqual(snapshot.records[0].details["Confidence"], "n")
    XCTAssertEqual(snapshot.records[0].details["Radiative power"], "0.0 MW")
    XCTAssertThrowsError(try ProviderParsers.fires(Data("Invalid MAP_KEY".utf8)))
  }
  func testCameraAdmissionAndMetadataDoNotInventFrameTimes() throws {
    XCTAssertNil(CameraProvider.admit("https://localhost/frame.jpg"))
    XCTAssertNil(CameraProvider.admit("https://s3-eu-west-1.amazonaws.com/arbitrary/x.jpg"))
    XCTAssertNil(
      CameraProvider.admit("https://user@s3-eu-west-1.amazonaws.com/jamcams.tfl.gov.uk/x.jpg"))
    let data = try json([
      [
        "id": "one", "lat": 0, "lon": 0, "commonName": "Public camera",
        "additionalProperties": [
          [
            "key": "imageUrl",
            "value": "https://s3-eu-west-1.amazonaws.com/jamcams.tfl.gov.uk/one.jpg",
          ]
        ],
      ]
    ])
    let record = try XCTUnwrap(ProviderParsers.cameras(data, provider: .london).records.first)
    XCTAssertEqual(record.coordinate, Coordinate(latitude: 0, longitude: 0))
    XCTAssertNil(record.observedAt)
  }
  func testSentinelPreservesTimeFootprintAndUnknownCloud() throws {
    let geometry: [String: Any] = [
      "type": "Polygon", "coordinates": [[[0, 0], [1, 0], [1, 1], [0, 0]]],
    ]
    let data = try json([
      "value": [
        [
          "Id": "94b894fc-312e-4152-b2ab-ce648768aa84", "Name": "Actual product",
          "ContentDate": ["Start": "2026-09-20T03:10:00Z"], "GeoFootprint": geometry,
        ]
      ]
    ])
    let record = try XCTUnwrap(
      ProviderParsers.sentinel(
        data, center: Coordinate(latitude: 0, longitude: 0)!, kind: "SENTINEL-1"
      ).records.first)
    XCTAssertEqual(record.footprint.count, 4)
    XCTAssertNil(record.details["Cloud cover"])
    XCTAssertEqual(record.observedAt, ProviderParsers.date("2026-09-20T03:10:00Z"))
  }
  func testWorkspaceRoundTripExcludesNotesFromLinksAndRejectsBadImports() throws {
    var state = WorkspaceState()
    state.layers = [.satellites, .cameras]
    state.basemap = .streets
    state.style = .thermal
    state.annotations = [.init(title: "My note", points: [Camera.initial.center])]
    let restored = try XCTUnwrap(WorkspaceState.from(try state.shareURL()))
    XCTAssertEqual(restored.layers, state.layers)
    XCTAssertEqual(restored.style, .thermal)
    XCTAssertTrue(restored.annotations.isEmpty)
    XCTAssertEqual(
      try JSONDecoder().decode(WorkspaceState.self, from: JSONEncoder().encode(state)).validated()
        .annotations.count, 1)
    state.annotations = [.init(title: "", points: [])]
    XCTAssertThrowsError(try state.validated())
    state.annotations = []
    state.version = 99
    XCTAssertThrowsError(try state.validated())
    XCTAssertNil(WorkspaceState.from(URL(string: "helios://workspace?state=x&state=y")!))
    XCTAssertNil(WorkspaceState.from(URL(string: "helios://workspace?state=notbase64")!))
  }
  func testHistoryRejectsOutOfOrderFixesAndNeverExtrapolates() {
    let now = Date()
    let point = Camera.initial.center
    func record(_ time: Date?, _ altitude: Double) -> GeoRecord {
      .init(
        id: "target", feed: .flights, label: "Actual target", coordinate: point, altitude: altitude,
        observedAt: time, source: "Fixture", details: [:])
    }
    var history = TrackHistory()
    history.ingest([record(now.addingTimeInterval(-10), 1000)], now: now)
    history.ingest([record(now.addingTimeInterval(-20), 2000), record(nil, 3000)], now: now)
    history.ingest([record(now, 4000)], now: now)
    XCTAssertEqual(history.fixes(for: "target").count, 2)
    XCTAssertNil(history.delayed(record(now, 4000), at: now.addingTimeInterval(-30)))
    XCTAssertEqual(
      history.delayed(record(now, 4000), at: now.addingTimeInterval(-5))?.altitude, 1000)
  }
  func testAISDoesNotUseSentinelCoordinatesOrUnknownSpeed() throws {
    let data = try json([
      "MessageType": "PositionReport",
      "MetaData": ["MMSI": 123_456_789, "ShipName": "Actual ship"],
      "Message": ["PositionReport": ["Latitude": 0, "Longitude": 0, "Sog": 102.3, "Cog": 360]],
    ])
    let record = try XCTUnwrap(AISParser.record(data))
    XCTAssertNil(record.observedAt)
    XCTAssertNil(record.details["Speed"])
    XCTAssertNil(record.details["Course"])
    XCTAssertEqual(record.coordinate, Coordinate(latitude: 0, longitude: 0))
    XCTAssertThrowsError(try AISParser.record(json(["error": "Invalid key"])))
  }
  func testRoutingRejectsMalformedGeometryAndKeepsActualTurns() throws {
    let data = try json([
      "code": "Ok",
      "routes": [
        [
          "distance": 50, "duration": 10,
          "geometry": ["type": "LineString", "coordinates": [[0, 0], [0.001, 0]]],
          "legs": [
            [
              "steps": [
                [
                  "distance": 50, "name": "Actual road",
                  "maneuver": ["type": "depart", "location": [0, 0]],
                ]
              ]
            ]
          ],
        ]
      ],
    ])
    let route = try NativeRoute.parse(data)
    XCTAssertEqual(route.points.count, 2)
    XCTAssertEqual(route.steps.count, 1)
    XCTAssertTrue(route.steps[0].instruction.contains("Actual road"))
    XCTAssertThrowsError(try NativeRoute.parse(json(["code": "NoRoute"])))
  }
}
