import Foundation

public actor AdditionalSources {
  private let transport: Transport
  private var mappedCache: [String: (Date, Snapshot)] = [:]
  public init(transport: Transport = Transport()) { self.transport = transport }
  public func shutdown() { transport.session.invalidateAndCancel() }
  public func snapshot(feed: FeedKind, camera: Camera, aspect: Double)
    async throws -> Snapshot
  {
    switch feed {
    case .installations, .energy:
      let bounds = try GeoBounds.viewport(
        camera: camera, aspect: aspect, maximumSpan: feed == .energy ? 2 : 10)
      let query = try MappedSites.query(feed: feed, bounds: bounds)
      if let cached = mappedCache[query], Date().timeIntervalSince(cached.0) < 300 {
        return cached.1
      }
      var last: (any Error) = FeedError.malformed
      for host in [
        "overpass-api.de", "overpass.kumi.systems", "lz4.overpass-api.de",
        "overpass.private.coffee",
      ] {
        do {
          let url = URL(string: "https://\(host)/api/interpreter")!
          let data = try await transport.data(
            url, maximumBytes: 16 * 1_024 * 1_024, allowRedirects: false, form: ["data": query],
            timeout: 30)
          let snapshot = try MappedSites.parse(data, feed: feed, bounds: bounds)
          try Task.checkCancellation()
          if mappedCache.count >= 32,
            let oldest = mappedCache.min(by: { $0.value.0 < $1.value.0 })?.key
          {
            mappedCache.removeValue(forKey: oldest)
          }
          mappedCache[query] = (Date(), snapshot)
          return snapshot
        } catch {
          try Task.checkCancellation()
          last = error
        }
      }
      throw last
    default: throw FeedError.malformed
    }
  }
}
