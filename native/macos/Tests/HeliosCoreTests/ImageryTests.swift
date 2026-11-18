import XCTest

@testable import HeliosCore

final class ImageryTests: XCTestCase {
  func testYesterdayIsUTCAndCrossesMonthAndYear() {
    let formatter = ISO8601DateFormatter()
    XCTAssertEqual(
      DailyImagery.date(at: formatter.date(from: "2026-01-01T00:05:00Z")!), "2025-12-31")
    XCTAssertEqual(
      DailyImagery.date(at: formatter.date(from: "2026-10-01T23:59:00Z")!), "2026-09-30")
  }
  func testSourceURLsAndResolutionLimitsAreExplicit() {
    let tile = TileID(x: 4, y: 2, zoom: 3)!
    for source in DailyImagery.allCases where source != .off {
      let url = source.url(tile: tile, date: "2026-09-27")!
      XCTAssertEqual(url.host, "gibs.earthdata.nasa.gov")
      XCTAssertEqual(url.scheme, "https")
      XCTAssertTrue(url.path.contains("/2026-09-27/GoogleMapsCompatible_Level9/3/2/4."))
      XCTAssertNil(source.url(tile: TileID(x: 1, y: 1, zoom: 10)!, date: "2026-09-27"))
      XCTAssertNil(source.url(tile: tile, date: "../../../"))
    }
    XCTAssertNil(DailyImagery.off.url(tile: tile, date: "2026-09-27"))
    XCTAssertEqual(RasterKey(tile: tile).url, tile.url)
    XCTAssertTrue(
      DailyImagery.nightLights.url(tile: tile, date: "2026-09-27")!.path.hasSuffix(".png"))
  }
  func testSatelliteTilesRemainCappedWhenZoomedToStreetLevel() {
    let tiles = TileID.visible(
      camera: Camera(center: Coordinate(latitude: 45, longitude: 9)!, altitude: 500), maximumZoom: 9
    )
    XCTAssertTrue(tiles.allSatisfy { $0.zoom <= 9 })
    XCTAssertLessThanOrEqual(tiles.count, 81)
    XCTAssertNotEqual(
      RasterKey(tile: tiles[0], imagery: .thermal, date: "2026-09-27"),
      RasterKey(tile: tiles[0], imagery: .thermal, date: "2026-09-28"))
  }
}
