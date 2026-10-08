import XCTest
import simd

@testable import GodEyeCore

final class GeographyTests: XCTestCase {
  func testWGS84EquatorAndPole() {
    let equator = Coordinate(latitude: 0, longitude: 0)!.ecef()
    XCTAssertEqual(equator.x, 6_378_137, accuracy: 0.001)
    XCTAssertEqual(equator.y, 0)
    XCTAssertEqual(equator.z, 0)
    let pole = Coordinate(latitude: 90, longitude: 123)!.ecef()
    XCTAssertEqual(pole.z, 6_356_752.314245, accuracy: 0.001)
    XCTAssertEqual(
      simd_length(Coordinate(latitude: 0, longitude: 0)!.ecef(altitude: 1_000)), 6_379_137,
      accuracy: 0.001)
  }
  func testInvalidCoordinatesFailClosed() {
    for (lat, lon) in [(91.0, 0.0), (0, 181), (Double.nan, 0), (0, Double.infinity)] {
      XCTAssertNil(Coordinate(latitude: lat, longitude: lon))
    }
    XCTAssertNotNil(Coordinate(latitude: 0, longitude: 0))
    XCTAssertThrowsError(
      try JSONDecoder().decode(Coordinate.self, from: Data(#"{"latitude":91,"longitude":0}"#.utf8)))
  }
  func testCameraZoomAndPanBounds() {
    var camera = Camera.initial
    for _ in 0..<300 { camera.zoom(delta: 200) }
    XCTAssertEqual(camera.altitude, 500)
    for _ in 0..<300 {
      camera.zoom(delta: -200)
      camera.pan(dx: 20, dy: 50)
    }
    XCTAssertEqual(camera.altitude, 60_000_000)
    XCTAssertTrue((-180...180).contains(camera.center.longitude))
    XCTAssertTrue((-85...85).contains(camera.center.latitude))
  }
  func testOriginalHorizontalAndInvertedVerticalPanWithRotatedCamera() {
    let origin = Coordinate(latitude: 0, longitude: 0)!
    for heading in [0.0, 90, 180, 270] {
      var right = Camera(center: origin, altitude: 100_000, heading: heading)
      right.pan(dx: 10, dy: 0)
      var up = Camera(center: origin, altitude: 100_000, heading: heading)
      up.pan(dx: 0, dy: 10)
      let scale = 100_000.0 / 6_378_137 / 8
      let angle = heading * Double.pi / 180
      XCTAssertEqual(right.center.latitude, 10 * sin(angle) * scale, accuracy: 1e-9)
      XCTAssertEqual(right.center.longitude, -10 * cos(angle) * scale, accuracy: 1e-9)
      XCTAssertEqual(up.center.latitude, -10 * cos(angle) * scale, accuracy: 1e-9)
      XCTAssertEqual(up.center.longitude, -10 * sin(angle) * scale, accuracy: 1e-9)
      right.pan(dx: -10, dy: 0)
      XCTAssertEqual(right.center.latitude, 0, accuracy: 1e-9)
      XCTAssertEqual(right.center.longitude, 0, accuracy: 1e-9)
    }
  }
  func testInvalidCameraInputCannotCorruptNavigation() throws {
    var camera = Camera.initial
    camera.pan(dx: .nan, dy: 0)
    camera.pan(dx: 0, dy: .infinity)
    camera.zoom(delta: .nan)
    XCTAssertEqual(camera, Camera.initial)
    XCTAssertThrowsError(
      try JSONDecoder().decode(
        Camera.self, from: Data(#"{"center":{"latitude":0,"longitude":0},"altitude":-1}"#.utf8)))
    XCTAssertEqual(
      try JSONDecoder().decode(Camera.self, from: JSONEncoder().encode(camera)), camera)
  }
  func testShareRoundTripAndHostAdmission() {
    let original = Camera(center: Coordinate(latitude: -33.8, longitude: 151.2)!, altitude: 42_000)
    XCTAssertEqual(Camera.fromShareURL(original.shareURL), original)
    for text in [
      "https://view?lat=0&lon=0&alt=500", "godeye://evil?lat=0&lon=0&alt=500",
      "godeye://view?lat=91&lon=0&alt=500", "godeye://view?lat=0&lat=1&lon=0&alt=500",
      "godeye://view?lat=0&lon=0&alt=nan", "godeye://view?lat=0&lon=0&alt=0",
    ] { XCTAssertNil(Camera.fromShareURL(URL(string: text)!)) }
  }
  func testMercatorTileCoordinatesAndDateLine() {
    let tile = TileID.containing(Coordinate(latitude: 0, longitude: 0)!, zoom: 1)
    XCTAssertEqual(tile.x, 1)
    XCTAssertEqual(tile.y, 1)
    XCTAssertEqual(tile.coordinate(u: 0, v: 0).longitude, 0)
    XCTAssertEqual(tile.coordinate(u: 0, v: 0).latitude, 0, accuracy: 0.000001)
    XCTAssertEqual(TileID(x: -1, y: 0, zoom: 2)?.x, 3)
    XCTAssertEqual(TileID.containing(Coordinate(latitude: 0, longitude: 180)!, zoom: 4).x, 0)
    XCTAssertNotNil(TileID.containing(Coordinate(latitude: 90, longitude: 0)!, zoom: 18))
    XCTAssertNil(TileID(x: 0, y: -1, zoom: 2))
    XCTAssertNil(TileID(x: 0, y: 0, zoom: 30))
  }
  func testVisibleTilesAreBoundedUniqueAndSecure() {
    for coordinate in [
      Coordinate(latitude: 0, longitude: 179.99)!, Coordinate(latitude: 85, longitude: 0)!,
    ] {
      let tiles = TileID.visible(camera: Camera(center: coordinate, altitude: 500))
      XCTAssertLessThanOrEqual(tiles.count, 81)
      XCTAssertEqual(Set(tiles).count, tiles.count)
      XCTAssertTrue(
        tiles.allSatisfy { $0.url.scheme == "https" && $0.url.host == "server.arcgisonline.com" })
    }
  }
}
