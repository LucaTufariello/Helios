import Foundation

public enum AtmosphereParser {
  public static func parse(
    _ data: Data, coordinate: Coordinate, airQuality: Data? = nil, airError: String? = nil
  ) throws -> Snapshot {
    guard let root = try JSONSerialization.jsonObject(with: data) as? [String: Any],
      let current = root["current"] as? [String: Any],
      let units = root["current_units"] as? [String: Any],
      let temperature = Parsers.number(current["temperature_2m"])
    else { throw FeedError.malformed }
    var details: [String: String] = [
      "Method": "Weather model for the requested point; not a station observation"
    ]
    let fields = [
      ("temperature_2m", "Temperature"), ("apparent_temperature", "Feels like"),
      ("relative_humidity_2m", "Humidity"), ("wind_speed_10m", "Wind speed"),
      ("wind_direction_10m", "Wind direction"), ("surface_pressure", "Surface pressure"),
    ]
    for (key, label) in fields {
      if let value = Parsers.number(current[key]) {
        details[label] =
          String(format: "%.1f", value) + " " + (units[key] as? String ?? "unit unavailable")
      }
    }
    if let daily = root["daily"] as? [String: Any], let dates = daily["time"] as? [String] {
      for (index, date) in dates.prefix(2).enumerated() {
        for (key, label) in [
          ("temperature_2m_max", "Maximum"), ("temperature_2m_min", "Minimum"),
          ("precipitation_probability_max", "Precipitation probability"),
        ] {
          if let values = daily[key] as? [Any], index < values.count,
            let value = Parsers.number(values[index])
          {
            let dailyUnits = root["daily_units"] as? [String: Any]
            details["\(date) · \(label)"] =
              String(format: "%.1f", value) + " " + (dailyUnits?[key] as? String ?? "")
          }
        }
      }
    }
    if let code = Parsers.jsonNumber(current["weather_code"]), (0...99).contains(code),
      code.rounded(.towardZero) == code, let label = weatherLabels[Int(code)]
    {
      details["Conditions"] = label
    }
    if let airQuality,
      let air = try? JSONSerialization.jsonObject(with: airQuality) as? [String: Any],
      let currentAir = air["current"] as? [String: Any],
      let airUnits = air["current_units"] as? [String: Any]
    {
      var available = false
      for (key, label) in [
        ("european_aqi", "European AQI"), ("us_aqi", "US AQI"), ("pm2_5", "PM2.5"),
        ("pm10", "PM10"), ("nitrogen_dioxide", "NO₂"), ("ozone", "Ozone"),
      ] {
        if let value = Parsers.jsonNumber(currentAir[key]), value >= 0 {
          available = true
          details[label] =
            String(format: "%.1f", value) + " " + (airUnits[key] as? String ?? "unit unavailable")
        }
      }
      details["Air quality"] =
        available
        ? "CAMS / Open-Meteo model; not a station observation"
        : "Source returned no usable air-quality values"
      if let timestamp = currentAir["time"] as? String {
        details["Air model time (UTC)"] = timestamp
      }
    } else {
      details["Air quality"] = airError.map { "Unavailable: \($0)" } ?? "Unavailable"
    }
    let time = (current["time"] as? String).flatMap {
      ISO8601DateFormatter().date(from: $0.hasSuffix("Z") ? $0 : $0 + ":00Z")
    }
    let record = GeoRecord(
      id: "atmosphere:camera-point", feed: .atmosphere,
      label: String(
        format: "Modelled weather · %.1f %@", temperature, units["temperature_2m"] as? String ?? ""),
      coordinate: coordinate, altitude: nil, observedAt: time, source: "Open-Meteo model",
      details: details)
    return Snapshot(records: [record], rejected: 0, source: "Open-Meteo model")
  }
  private static let weatherLabels: [Int: String] = [
    0: "Clear sky", 1: "Mainly clear", 2: "Partly cloudy", 3: "Overcast", 45: "Fog",
    48: "Depositing rime fog",
    51: "Light drizzle", 53: "Drizzle", 55: "Dense drizzle", 56: "Light freezing drizzle",
    57: "Dense freezing drizzle",
    61: "Slight rain", 63: "Rain", 65: "Heavy rain", 66: "Light freezing rain",
    67: "Heavy freezing rain",
    71: "Slight snow", 73: "Snow", 75: "Heavy snow", 77: "Snow grains", 80: "Slight showers",
    81: "Showers",
    82: "Violent showers", 85: "Slight snow showers", 86: "Heavy snow showers", 95: "Thunderstorm",
    96: "Thunderstorm, slight hail", 99: "Thunderstorm, heavy hail",
  ]
}

public enum DisasterParser {
  public static func parse(_ data: Data) throws -> Snapshot {
    guard data.range(of: Data("<!DOCTYPE".utf8)) == nil,
      data.range(of: Data("<!ENTITY".utf8)) == nil
    else { throw FeedError.malformed }
    let delegate = RSSDelegate()
    let parser = XMLParser(data: data)
    parser.shouldResolveExternalEntities = false
    parser.delegate = delegate
    guard parser.parse(), delegate.sawChannel else { throw FeedError.malformed }
    return Snapshot(records: delegate.records, rejected: delegate.rejected, source: "GDACS")
  }
}
private final class RSSDelegate: NSObject, XMLParserDelegate {
  var records: [GeoRecord] = [], rejected = 0, sawChannel = false
  private var fields: [String: String] = [:]
  private var current: String?
  private var inItem = false
  private var seen = Set<String>()
  func parser(
    _ parser: XMLParser, didStartElement elementName: String, namespaceURI: String?,
    qualifiedName qName: String?, attributes attributeDict: [String: String] = [:]
  ) {
    if elementName == "channel" { sawChannel = true }
    if elementName == "item" {
      inItem = true
      fields = [:]
    } else if inItem {
      current = elementName
    }
  }
  func parser(_ parser: XMLParser, foundCharacters string: String) {
    if let current, inItem, (fields[current]?.count ?? 0) < 4_096 {
      fields[current, default: ""] += string
    }
  }
  func parser(_ parser: XMLParser, foundCDATA cdataBlock: Data) {
    if let text = String(data: cdataBlock, encoding: .utf8) {
      self.parser(parser, foundCharacters: text)
    }
  }
  func parser(
    _ parser: XMLParser, didEndElement elementName: String, namespaceURI: String?,
    qualifiedName qName: String?
  ) {
    if elementName == "item" {
      finish()
      inItem = false
    }
    current = nil
    if records.count + rejected > 2_000 { parser.abortParsing() }
  }
  private func finish() {
    let title = String(
      (fields["title"] ?? "").trimmingCharacters(in: .whitespacesAndNewlines).prefix(180))
    let link = (fields["link"] ?? "").trimmingCharacters(in: .whitespacesAndNewlines)
    var lat = Parsers.number(fields["geo:lat"])
    var lon = Parsers.number(fields["geo:long"])
    if lat == nil || lon == nil {
      let point = (fields["georss:point"] ?? "").split(whereSeparator: { $0.isWhitespace })
      if point.count == 2 {
        lat = Parsers.number(String(point[0]))
        lon = Parsers.number(String(point[1]))
      }
    }
    guard !title.isEmpty, let url = URL(string: link), url.host == "www.gdacs.org",
      ["http", "https"].contains(url.scheme ?? ""), let lat, let lon,
      let coordinate = Coordinate(latitude: lat, longitude: lon)
    else {
      rejected += 1
      return
    }
    let id = (fields["guid"] ?? link).trimmingCharacters(in: .whitespacesAndNewlines)
    guard !id.isEmpty, seen.insert(id).inserted else {
      rejected += 1
      return
    }
    let formatter = DateFormatter()
    formatter.locale = Locale(identifier: "en_US_POSIX")
    formatter.dateFormat = "EEE, dd MMM yyyy HH:mm:ss Z"
    let date = formatter.date(from: fields["pubDate"] ?? "")
    var details = [
      "Source URL": link, "Timestamp meaning": "Alert publication time", "Severity": "Unknown",
    ]
    let severity = title.components(separatedBy: " ").first?.lowercased() ?? ""
    if ["red", "orange", "green"].contains(severity) { details["Severity"] = severity.capitalized }
    records.append(
      .init(
        id: "gdacs:\(id)", feed: .disasters, label: title, coordinate: coordinate, altitude: nil,
        observedAt: date, source: "GDACS", details: details))
  }
}
