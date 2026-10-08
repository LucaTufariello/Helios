import Foundation

/// Per-request bounded native transport. No local web server, browser, or JS runtime.
public struct Transport: Sendable {
  public let session: URLSession
  public init(session: URLSession? = nil) {
    if let session {
      self.session = session
    } else {
      let config = URLSessionConfiguration.ephemeral
      config.timeoutIntervalForRequest = 20
      config.timeoutIntervalForResource = 30
      config.httpMaximumConnectionsPerHost = 4
      config.urlCache = nil
      self.session = URLSession(configuration: config)
    }
  }
  public func data(
    _ url: URL, maximumBytes: Int = 16 * 1_024 * 1_024, allowRedirects: Bool = true,
    form: [String: String]? = nil, timeout: TimeInterval? = nil, headers: [String: String] = [:]
  )
    async throws -> Data
  {
    guard url.scheme == "https" else { throw FeedError.insecure }
    var request = URLRequest(url: url)
    if let timeout { request.timeoutInterval = timeout }
    if let form {
      var encoded = URLComponents()
      encoded.queryItems = form.sorted { $0.key < $1.key }.map {
        URLQueryItem(name: $0.key, value: $0.value)
      }
      request.httpMethod = "POST"
      request.setValue("application/x-www-form-urlencoded", forHTTPHeaderField: "Content-Type")
      request.httpBody = encoded.percentEncodedQuery?.replacingOccurrences(of: "+", with: "%2B")
        .data(using: .utf8)
    }
    request.setValue(
      "GodEyeNative/0.1 (public-data macOS client)", forHTTPHeaderField: "User-Agent")
    for (name, value) in headers { request.setValue(value, forHTTPHeaderField: name) }
    let (bytes, response) = try await session.bytes(
      for: request, delegate: allowRedirects ? nil : NoRedirects())
    defer { bytes.task.cancel() }
    guard let response = response as? HTTPURLResponse else { throw FeedError.malformed }
    guard response.url?.scheme == "https" else { throw FeedError.insecure }
    guard (200..<300).contains(response.statusCode) else {
      throw FeedError.http(response.statusCode)
    }
    guard response.expectedContentLength <= Int64(maximumBytes) else { throw FeedError.oversized }
    var result = Data()
    result.reserveCapacity(min(maximumBytes, max(0, Int(response.expectedContentLength))))
    return try await withTaskCancellationHandler {
      for try await byte in bytes {
        if result.count >= maximumBytes { throw FeedError.oversized }
        result.append(byte)
        if result.count % 16_384 == 0 { try Task.checkCancellation() }
      }
      try Task.checkCancellation()
      return result
    } onCancel: {
      bytes.task.cancel()
    }
  }
  public func snapshot(_ feed: FeedKind, center: Coordinate) async throws -> Snapshot {
    switch feed {
    case .installations, .energy, .satellites, .launches, .vessels, .fires, .transit, .cameras,
      .datacenters, .dams, .cables, .sentinel, .annotations, .traffic:
      throw FeedError.coverage("This source requires a native source context.")
    case .earthquakes:
      return try Parsers.earthquakes(
        await data(
          URL(string: "https://earthquake.usgs.gov/earthquakes/feed/v1.0/summary/all_day.geojson")!)
      )
    case .disasters:
      return try DisasterParser.parse(
        await data(
          URL(string: "https://www.gdacs.org/xml/rss.xml")!, maximumBytes: 2 * 1_024 * 1_024))
    case .atmosphere:
      var url = URLComponents(string: "https://api.open-meteo.com/v1/forecast")!
      url.queryItems = [
        .init(name: "latitude", value: String(center.latitude)),
        .init(name: "longitude", value: String(center.longitude)),
        .init(name: "timezone", value: "UTC"),
        .init(
          name: "current",
          value:
            "temperature_2m,relative_humidity_2m,apparent_temperature,weather_code,wind_speed_10m,wind_direction_10m,surface_pressure"
        ),
        .init(
          name: "daily",
          value: "temperature_2m_max,temperature_2m_min,precipitation_probability_max"),
        .init(name: "forecast_days", value: "2"),
      ]
      async let forecast = data(url.url!, maximumBytes: 2 * 1_024 * 1_024)
      async let quality = airQuality(center)
      let (air, airError) = await quality
      return try AtmosphereParser.parse(
        try await forecast, coordinate: center, airQuality: air, airError: airError)
    case .military:
      return try Parsers.adsb(
        await data(URL(string: "https://api.adsb.lol/v2/mil")!), feed: .military)
    case .flights:
      do {
        return try Parsers.openSky(
          await data(URL(string: "https://opensky-network.org/api/states/all?extended=1")!))
      } catch is CancellationError { throw CancellationError() } catch {
        try Task.checkCancellation()
        // Same keyless regional fallback family as the existing implementation.
        let url = URL(
          string: "https://api.adsb.lol/v2/lat/\(center.latitude)/lon/\(center.longitude)/dist/125")!
        return try Parsers.adsb(await data(url), feed: .flights)
      }
    }
  }
  private func airQuality(_ center: Coordinate) async -> (Data?, String?) {
    var url = URLComponents(string: "https://air-quality-api.open-meteo.com/v1/air-quality")!
    url.queryItems = [
      .init(name: "latitude", value: String(center.latitude)),
      .init(name: "longitude", value: String(center.longitude)),
      .init(name: "timezone", value: "UTC"),
      .init(name: "current", value: "european_aqi,us_aqi,pm2_5,pm10,nitrogen_dioxide,ozone"),
    ]
    do { return (try await data(url.url!, maximumBytes: 2 * 1_024 * 1_024), nil) } catch {
      return (nil, error.localizedDescription)
    }
  }
  public func search(_ query: String) async throws -> [(String, Coordinate)] {
    let query = query.trimmingCharacters(in: .whitespacesAndNewlines)
    guard !query.isEmpty, query.count <= 200 else { return [] }
    var url = URLComponents(string: "https://photon.komoot.io/api/")!
    url.queryItems = [.init(name: "q", value: query), .init(name: "limit", value: "6")]
    let payload = try JSONSerialization.jsonObject(
      with: await data(url.url!, maximumBytes: 1_024 * 1_024))
    guard let root = payload as? [String: Any], let features = root["features"] as? [[String: Any]]
    else { throw FeedError.malformed }
    return features.compactMap { row in
      guard let geometry = row["geometry"] as? [String: Any],
        let coords = geometry["coordinates"] as? [Any], coords.count >= 2,
        let lon = Parsers.number(coords[0]), let lat = Parsers.number(coords[1]),
        let point = Coordinate(latitude: lat, longitude: lon),
        let properties = row["properties"] as? [String: Any]
      else { return nil }
      let names = ["name", "city", "country"].compactMap { properties[$0] as? String }
      return (names.joined(separator: ", "), point)
    }
  }
}

private final class NoRedirects: NSObject, URLSessionTaskDelegate, Sendable {
  func urlSession(
    _ session: URLSession, task: URLSessionTask,
    willPerformHTTPRedirection response: HTTPURLResponse, newRequest request: URLRequest,
    completionHandler: @escaping @Sendable (URLRequest?) -> Void
  ) { completionHandler(nil) }
}
