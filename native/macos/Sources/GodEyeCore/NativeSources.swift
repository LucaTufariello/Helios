import Foundation

public struct SourceConfiguration: Sendable {
  public var satelliteGroup = SatelliteGroup.stations
  public var transitID = "mbta"
  public var cameraProvider = CameraProvider.london
  public var sentinelKind = "SENTINEL-2"
  public var firmsKey = ""
  public init() {}
}
public actor NativeSources {
  private let transport: Transport
  private var catalogs: [SatelliteGroup: OrbitalCatalog] = [:]
  private var references: [FeedKind: Snapshot] = [:]
  public init(transport: Transport = Transport()) { self.transport = transport }
  public func shutdown() { transport.session.invalidateAndCancel() }
  public func orbit(noradID: String, group: SatelliteGroup, at date: Date) throws -> ScenePath {
    guard
      let satellite = catalogs[group]?.satellites.first(where: {
        "norad:\($0.noradIdent)" == noradID
      })
    else { throw FeedError.coverage("Refresh the satellite catalog before requesting an orbit.") }
    return try satellite.estimatedOrbit(at: date)
  }
  public func snapshot(_ feed: FeedKind, center: Coordinate, configuration: SourceConfiguration)
    async throws -> Snapshot
  {
    switch feed {
    case .datacenters, .dams, .cables:
      if let cached = references[feed] { return cached }
      let snapshot = try ReferenceLayers.load(feed)
      references[feed] = snapshot
      return snapshot
    case .satellites:
      let group = configuration.satelliteGroup
      if let catalog = catalogs[group], Date().timeIntervalSince(catalog.retrievedAt) < 6 * 3600 {
        return catalog.snapshot()
      }
      var url = URLComponents(string: "https://celestrak.org/NORAD/elements/gp.php")!
      url.queryItems = [
        .init(name: "GROUP", value: group.rawValue), .init(name: "FORMAT", value: "tle"),
      ]
      let catalog = try OrbitalCatalog.parse(
        await transport.data(url.url!, maximumBytes: 8 * 1024 * 1024))
      try Task.checkCancellation()
      catalogs[group] = catalog
      return catalog.snapshot()
    case .launches:
      var url = URLComponents(string: "https://ll.thespacedevs.com/2.3.0/launches/")!
      let now = Date()
      let formatter = ISO8601DateFormatter()
      url.queryItems = [
        .init(name: "net__gte", value: formatter.string(from: now.addingTimeInterval(-30 * 86400))),
        .init(name: "net__lte", value: formatter.string(from: now)),
        .init(name: "limit", value: "100"), .init(name: "mode", value: "detailed"),
      ]
      return try ProviderParsers.launches(
        await transport.data(url.url!, maximumBytes: 12 * 1024 * 1024))
    case .transit:
      guard
        let provider = TransitOperator.catalog.first(where: { $0.id == configuration.transitID }),
        let url = URL(string: provider.url)
      else { throw FeedError.coverage("Choose a registered public transit operator.") }
      return try TransitParser.parse(
        await transport.data(url, maximumBytes: 16 * 1024 * 1024, headers: provider.headers),
        provider: provider)
    case .cameras:
      return try ProviderParsers.cameras(
        await transport.data(
          configuration.cameraProvider.catalogURL, maximumBytes: 8 * 1024 * 1024,
          allowRedirects: false,
          headers: [
            "Accept": "application/json", "Digitraffic-User": "GodEyeNative/0.1",
            "Accept-Encoding": "gzip",
          ]),
        provider: configuration.cameraProvider)
    case .sentinel:
      guard ["SENTINEL-1", "SENTINEL-2"].contains(configuration.sentinelKind) else {
        throw FeedError.malformed
      }
      var url = URLComponents(
        string: "https://catalogue.dataspace.copernicus.eu/odata/v1/Products")!
      url.queryItems = [
        .init(
          name: "$filter",
          value:
            "Collection/Name eq '\(configuration.sentinelKind)' and OData.CSC.Intersects(area=geography'SRID=4326;POINT(\(center.longitude) \(center.latitude))')"
        ), .init(name: "$top", value: "10"),
        .init(name: "$orderby", value: "ContentDate/Start desc"),
        .init(name: "$expand", value: "Attributes"),
      ]
      return try ProviderParsers.sentinel(
        await transport.data(url.url!, maximumBytes: 4 * 1024 * 1024), center: center,
        kind: configuration.sentinelKind)
    case .fires:
      let key = configuration.firmsKey
      guard (8...128).contains(key.count),
        key.allSatisfy({ $0.isASCII && ($0.isLetter || $0.isNumber || $0 == "-") })
      else {
        throw FeedError.coverage(
          "Add a free NASA FIRMS MAP_KEY in Provider Settings. No detections are generated without provider data."
        )
      }
      let url = URL(
        string: "https://firms.modaps.eosdis.nasa.gov/api/area/csv/\(key)/VIIRS_SNPP_NRT/world/2")!
      return try ProviderParsers.fires(
        await transport.data(url, maximumBytes: 24 * 1024 * 1024, allowRedirects: false))
    default: throw FeedError.malformed
    }
  }
}
