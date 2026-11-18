import XCTest

@testable import HeliosCore

final class EnvironmentalTests: XCTestCase {
  func testDisasterGUIDAttributesEntitiesAndReportedZeros() throws {
    let xml = """
      <rss xmlns:geo="http://www.w3.org/2003/01/geo/wgs84_pos#"><channel>
      <item><guid isPermaLink="false">FL123</guid><title><![CDATA[Orange flood alert]]></title><link>https://www.gdacs.org/report.aspx?eventtype=FL&amp;eventid=123</link><geo:lat>0</geo:lat><geo:long>0</geo:long><pubDate>Mon, 28 Sep 2026 12:00:00 +0000</pubDate></item>
      <item><guid>FL124</guid><title>Missing position</title><link>https://www.gdacs.org/report.aspx?eventid=124</link></item>
      </channel></rss>
      """
    let result = try DisasterParser.parse(Data(xml.utf8))
    XCTAssertEqual(result.records.count, 1)
    XCTAssertEqual(result.rejected, 1)
    XCTAssertEqual(result.records.first?.id, "gdacs:FL123")
    XCTAssertEqual(result.records.first?.coordinate, Coordinate(latitude: 0, longitude: 0))
    XCTAssertTrue(result.records.first?.details["Source URL"]?.contains("&eventid=123") ?? false)
    XCTAssertEqual(result.records.first?.details["Severity"], "Orange")
    XCTAssertNotNil(result.records.first?.observedAt)
  }
  func testDisasterGeoRSSFallbackAndHostAdmission() throws {
    let xml = """
      <rss xmlns:georss="http://www.georss.org/georss"><channel>
      <item><guid>A</guid><title>Red earthquake alert</title><link>https://www.gdacs.org/one</link><georss:point>45 9</georss:point></item>
      <item><guid>B</guid><title>Bad host</title><link>https://example.com/one</link><georss:point>45 9</georss:point></item>
      <item><guid>C</guid><title>Bad coordinate</title><link>https://www.gdacs.org/one</link><georss:point>95 9</georss:point></item>
      </channel></rss>
      """
    let result = try DisasterParser.parse(Data(xml.utf8))
    XCTAssertEqual(result.records.count, 1)
    XCTAssertEqual(result.rejected, 2)
    XCTAssertNil(result.records.first?.observedAt)
    XCTAssertThrowsError(try DisasterParser.parse(Data("<html/>".utf8)))
    XCTAssertThrowsError(try DisasterParser.parse(Data("<rss><channel>".utf8)))
  }
  func testDisasterRejectsEntityDeclarations() {
    XCTAssertThrowsError(
      try DisasterParser.parse(
        Data("<!DOCTYPE rss [<!ENTITY x SYSTEM 'file:///tmp/private'>]><rss><channel/></rss>".utf8))
    )
  }
  func testAirQualityHasIndependentTimeUnitsAndDegradesWithoutLosingWeather() throws {
    let point = Coordinate(latitude: 0, longitude: 0)!
    let weather = Data(
      #"{"current":{"temperature_2m":12,"weather_code":3},"current_units":{"temperature_2m":"°C"}}"#
        .utf8)
    let air = Data(
      #"{"current":{"european_aqi":24,"pm2_5":8.1,"pm10":null,"ozone":false,"time":"2026-09-28T12:00"},"current_units":{"european_aqi":"EAQI","pm2_5":"μg/m³"}}"#
        .utf8)
    let result = try AtmosphereParser.parse(weather, coordinate: point, airQuality: air).records[0]
    XCTAssertEqual(result.details["European AQI"], "24.0 EAQI")
    XCTAssertEqual(result.details["PM2.5"], "8.1 μg/m³")
    XCTAssertNil(result.details["PM10"])
    XCTAssertNil(result.details["Ozone"])
    XCTAssertEqual(result.details["Conditions"], "Overcast")
    XCTAssertEqual(result.details["Air model time (UTC)"], "2026-09-28T12:00")
    XCTAssertNil(result.observedAt)
    let degraded = try AtmosphereParser.parse(weather, coordinate: point, airError: "HTTP 503")
    XCTAssertEqual(degraded.records.count, 1)
    XCTAssertTrue(degraded.records[0].details["Air quality"]!.contains("503"))
  }
  func testAtmosphereIsLabelledModelledWithOriginalUnitsAndForecast() throws {
    let root: [String: Any] = [
      "current": ["temperature_2m": 22, "time": "2026-09-28T12:00", "wind_speed_10m": 8],
      "current_units": ["temperature_2m": "°C", "wind_speed_10m": "km/h"],
      "daily": ["time": ["2026-09-28", "2026-09-29"], "temperature_2m_max": [24, 26]],
      "daily_units": ["temperature_2m_max": "°C"],
    ]
    let result = try AtmosphereParser.parse(
      JSONSerialization.data(withJSONObject: root),
      coordinate: Coordinate(latitude: 45, longitude: 9)!)
    XCTAssertEqual(result.records.count, 1)
    XCTAssertEqual(result.records.first?.source, "Open-Meteo model")
    XCTAssertEqual(result.records.first?.details["Wind speed"], "8.0 km/h")
    XCTAssertEqual(result.records.first?.details["2026-09-29 · Maximum"], "26.0 °C")
    XCTAssertNotNil(result.records.first?.observedAt)
    XCTAssertNil(result.records.first?.altitude)
    XCTAssertTrue(result.records.first?.details["Method"]?.contains("not a station") ?? false)
  }
  func testWeatherDoesNotInventMissingValuesOrTimestamps() throws {
    let root: [String: Any] = [
      "current": ["temperature_2m": 0], "current_units": ["temperature_2m": "°C"],
    ]
    let result = try AtmosphereParser.parse(
      JSONSerialization.data(withJSONObject: root),
      coordinate: Coordinate(latitude: 0, longitude: 0)!)
    XCTAssertNil(result.records.first?.observedAt)
    XCTAssertNil(result.records.first?.details["Humidity"])
    XCTAssertThrowsError(
      try AtmosphereParser.parse(
        Data(#"{"current":{"temperature_2m":null},"current_units":{}}"#.utf8),
        coordinate: Coordinate(latitude: 0, longitude: 0)!))
  }
}
