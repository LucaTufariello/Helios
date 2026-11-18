import Foundation

public enum SatelliteSource: String, Codable, CaseIterable, Sendable {
  case sentinel2, sentinel1, cbers4a, prisma
  public var shortName: String {
    switch self {
    case .sentinel2: "Sentinel-2"
    case .sentinel1: "Sentinel-1"
    case .cbers4a: "CBERS-4A WPM"
    case .prisma: "PRISMA"
    }
  }
  public var name: String {
    switch self {
    case .sentinel2: "Sentinel-2 · optical 10 m"
    case .sentinel1: "Sentinel-1 · SAR"
    case .cbers4a: "CBERS-4A WPM · pan 2 m / RGB 8 m"
    case .prisma: "PRISMA · pan 5 m / spectral 30 m"
    }
  }
}

/// Public Collection-1 L2A catalogue and true-colour COGs; no paid processing API.
public struct SentinelOpticalQuery: Codable, Equatable, Sendable {
  public let region: SentinelRegion
  public let start: Date
  public let end: Date
  public let maximumCloud: Double
  public var source: SatelliteSource = .sentinel2
  public init(
    region: SentinelRegion, start: Date, end: Date, maximumCloud: Double,
    source: SatelliteSource = .sentinel2
  ) throws {
    guard start.timeIntervalSince1970.isFinite, end.timeIntervalSince1970.isFinite,
      start <= end, end.timeIntervalSince(start) <= 366 * 86400,
      maximumCloud.isFinite, (0...100).contains(maximumCloud)
    else { throw SentinelOpticalError.query }
    self.region = region
    self.start = start
    self.end = end
    self.maximumCloud = maximumCloud
    self.source = source
  }
  public func url() throws -> URL {
    var calendar = Calendar(identifier: .gregorian)
    calendar.timeZone = TimeZone(secondsFromGMT: 0)!
    let first = calendar.startOfDay(for: start)
    let last = calendar.startOfDay(for: end).addingTimeInterval(86400 - 0.001)
    let formatter = ISO8601DateFormatter()
    formatter.formatOptions = [.withInternetDateTime, .withFractionalSeconds]
    let query = try JSONSerialization.data(withJSONObject: ["eo:cloud_cover": ["lte": maximumCloud]]
    )
    guard source == .sentinel2 || source == .cbers4a else { throw SentinelOpticalError.query }
    var url = URLComponents(
      string: source == .cbers4a
        ? "https://data.inpe.br/bdc/stac/v1/search"
        : "https://earth-search.aws.element84.com/v1/search")!
    url.queryItems = [
      .init(
        name: "collections", value: source == .cbers4a ? "CB4A-WPM-L4-DN-1" : "sentinel-2-c1-l2a"),
      .init(
        name: "bbox",
        value: [region.west, region.south, region.east, region.north].map { String($0) }.joined(
          separator: ",")),
      .init(
        name: "datetime", value: formatter.string(from: first) + "/" + formatter.string(from: last)),
      .init(name: "query", value: String(decoding: query, as: UTF8.self)),
      .init(name: "sortby", value: "-properties.datetime"), .init(name: "limit", value: "40"),
    ]
    if source == .cbers4a {
      // INPE reports null cloud cover for WPM. Never pretend this is 0% cloud.
      url.queryItems?.removeAll { $0.name == "query" || $0.name == "sortby" }
    }
    return url.url!
  }
}
public struct SentinelOpticalScene: Codable, Identifiable, Equatable, Sendable {
  public let id: String
  public let acquiredAt: String
  public let platform: String
  public let cloudCover: Double?
  public let bounds: [Double]
  public let visualURL: String
  public var source: SatelliteSource? = nil
  public var bands: [String: String]? = nil
  public var sensor: SatelliteSource { source ?? .sentinel2 }
  public var isValid: Bool {
    if sensor == .cbers4a {
      return id.range(
        of: #"^CBERS_4A_WPM_[0-9]{8}_[0-9]{3}_[0-9]{3}_L4$"#, options: .regularExpression) != nil
        && ProviderParsers.date(acquiredAt) != nil && ChangeReviewResult.validBounds(bounds)
        && (cloudCover == nil || cloudCover.map { $0.isFinite && (0...100).contains($0) } == true)
        && ["BAND0", "BAND1", "BAND2", "BAND3"].allSatisfy { key in
          bands?[key].map { Self.validCbersAsset($0, sceneID: id, band: key) } == true
        }
    }
    if sensor == .prisma {
      return id.range(of: #"^PRS_L2D_[A-Za-z0-9_]+$"#, options: .regularExpression) != nil
        && ProviderParsers.date(acquiredAt) != nil && ChangeReviewResult.validBounds(bounds)
        && visualURL.isEmpty && bands == nil
    }
    guard sensor == .sentinel2 else { return false }
    return
      id.range(of: #"^S2[ABC]_T[A-Z0-9]{5}_[0-9]{8}T[0-9]{6}_L2A$"#, options: .regularExpression)
      != nil
      && ProviderParsers.date(acquiredAt) != nil && platform.count <= 60
      && cloudCover.map { $0.isFinite && (0...100).contains($0) } == true
      && ChangeReviewResult.validBounds(bounds) && Self.validAsset(visualURL, sceneID: id)
  }
  /// The AWS public COG mirror has a deterministic path documented by CBERS on AWS.
  public static func cbersAsset(sceneID: String, band: String) -> String {
    let parts = sceneID.split(separator: "_")
    guard parts.count == 7,
      sceneID.range(
        of: #"^CBERS_4A_WPM_[0-9]{8}_[0-9]{3}_[0-9]{3}_L4$"#, options: .regularExpression) != nil
    else { return "" }
    return
      "https://brazil-eosats.s3.us-west-2.amazonaws.com/CBERS4A/WPM/\(parts[4])/\(parts[5])/\(sceneID)/\(sceneID)_\(band).tif"
  }
  public static func validCbersAsset(_ value: String, sceneID: String, band: String) -> Bool {
    ["BAND0", "BAND1", "BAND2", "BAND3", "BAND4"].contains(band)
      && value == cbersAsset(sceneID: sceneID, band: band)
  }
  public static func validAsset(_ value: String, sceneID: String) -> Bool {
    guard let url = URLComponents(string: value), url.scheme == "https",
      url.host == "e84-earth-search-sentinel-data.s3.us-west-2.amazonaws.com",
      url.user == nil, url.password == nil, url.port == nil, url.query == nil, url.fragment == nil,
      !url.percentEncodedPath.contains("%"), !url.path.contains(".."),
      url.path.hasPrefix("/sentinel-2-c1-l2a/"), url.path.hasSuffix("/\(sceneID)/TCI.tif")
    else { return false }
    return true
  }
  public static func parse(_ data: Data, source: SatelliteSource = .sentinel2) throws -> [Self] {
    guard data.count <= 8 * 1024 * 1024,
      let root = try JSONSerialization.jsonObject(with: data) as? [String: Any],
      let features = root["features"] as? [[String: Any]], features.count <= 100
    else { throw SentinelOpticalError.catalogue }
    if source == .cbers4a {
      return features.compactMap { feature in
        guard feature["collection"] as? String == "CB4A-WPM-L4-DN-1",
          let id = feature["id"] as? String, let bounds = feature["bbox"] as? [Double],
          let properties = feature["properties"] as? [String: Any],
          let acquired = properties["datetime"] as? String,
          let assets = feature["assets"] as? [String: [String: Any]],
          ["BAND0", "BAND1", "BAND2", "BAND3"].allSatisfy({ assets[$0] != nil })
        else { return nil }
        let scene = Self(
          id: id, acquiredAt: acquired, platform: "CBERS-4A WPM",
          cloudCover: Parsers.number(properties["eo:cloud_cover"]), bounds: bounds,
          visualURL: "", source: .cbers4a,
          bands: Dictionary(
            uniqueKeysWithValues: (0...4).map {
              let key = "BAND\($0)"
              return (key, Self.cbersAsset(sceneID: id, band: key))
            }))
        return scene.isValid ? scene : nil
      }
    }
    return features.compactMap { feature in
      guard feature["collection"] as? String == "sentinel-2-c1-l2a",
        let id = feature["id"] as? String, let bounds = feature["bbox"] as? [Double],
        let properties = feature["properties"] as? [String: Any],
        let acquired = properties["datetime"] as? String,
        let cloud = Parsers.number(properties["eo:cloud_cover"]),
        let assets = feature["assets"] as? [String: [String: Any]],
        let visual = assets["visual"]?["href"] as? String
      else { return nil }
      let scene = Self(
        id: id, acquiredAt: acquired,
        platform: properties["platform"] as? String ?? "Sentinel-2",
        cloudCover: cloud, bounds: bounds, visualURL: visual)
      return scene.isValid ? scene : nil
    }
  }
}
public enum SentinelOpticalError: Error, LocalizedError {
  case query, catalogue
  public var errorDescription: String? {
    switch self {
    case .query:
      "Select a region, a date range of up to one year and a cloud percentage from 0 to 100."
    case .catalogue: "The optical catalogue returned an unsupported response. Try again later."
    }
  }
}
public struct SentinelOpticalCatalog: Sendable {
  public let transport: Transport
  public init(transport: Transport = Transport()) { self.transport = transport }
  public func search(_ query: SentinelOpticalQuery) async throws -> [SentinelOpticalScene] {
    let data = try await transport.data(
      try query.url(), maximumBytes: 8 * 1024 * 1024, allowRedirects: false)
    // Recheck the cloud/date constraints rather than relying solely on upstream filters.
    return try SentinelOpticalScene.parse(data, source: query.source).filter {
      ($0.cloudCover.map { $0 <= query.maximumCloud } ?? true)
        && ProviderParsers.date($0.acquiredAt).map {
          var calendar = Calendar(identifier: .gregorian)
          calendar.timeZone = TimeZone(secondsFromGMT: 0)!
          return $0 >= calendar.startOfDay(for: query.start)
            && $0 < calendar.startOfDay(for: query.end).addingTimeInterval(86400)
        } == true
    }.sorted { $0.acquiredAt > $1.acquiredAt }
  }
}
public struct SentinelOpticalRequest: Codable, Sendable {
  public let scene: SentinelOpticalScene
  public let region: SentinelRegion
  public var mode: String? = nil
  public init(scene: SentinelOpticalScene, region: SentinelRegion) {
    self.scene = scene
    self.region = region
  }
}
public struct SentinelOpticalResult: Codable, Sendable {
  public var version: Int
  public var state: SarState
  public var createdAt: String
  public var workerPID: Int32?
  public var scene: SentinelOpticalScene?
  public var region: SentinelRegion?
  public var bounds: [Double]?
  public var width: Int?
  public var height: Int?
  public var crs: String?
  public var validFraction: Double?
  public var sourceResolutionMetres: [Double]?
  public var reflectanceScale: [Double]?
  public var originalInput: String?
  public var mode: String?
  public var wavelengthsNanometres: [Double]?
  public var displayRanges: [[Double]]?
  public var scientific: String?
  public var resampling: String?
  public var preview: String?
  public var geotiff: String?
  public var error: String?
  public var limitations: [String]?
  public var isValid: Bool {
    guard version == 1, ProviderParsers.date(createdAt) != nil else { return false }
    guard state == .completed else { return true }
    guard scene?.isValid == true, region != nil,
      scientific == nil || scientific == "source-values.tif", let bounds,
      ChangeReviewResult.validBounds(bounds),
      let width, let height, (1...2048).contains(width), (1...2048).contains(height),
      crs == "EPSG:4326", let validFraction, validFraction.isFinite,
      validFraction > 0, validFraction <= 1, preview == "preview.png", geotiff == "true-color.tif"
    else { return false }
    if scene?.sensor == .cbers4a {
      guard ["pan", "rgb"].contains(mode ?? ""), scientific == "source-values.tif" else {
        return false
      }
    }
    if scene?.sensor == .prisma {
      guard ["pan", "rgb", "spectral"].contains(mode ?? ""), scientific == "source-values.tif",
        let scale = reflectanceScale, scale.count == 2, scale.allSatisfy({ $0.isFinite }),
        scale[0] < scale[1]
      else { return false }
    }
    return true
  }
}
