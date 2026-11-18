import Foundation

/// Geographic search rectangle. Complete SAFE scenes are downloaded, never arbitrary pixel cutouts.
public struct SentinelRegion: Codable, Equatable, Sendable {
  public let west: Double
  public let south: Double
  public let east: Double
  public let north: Double
  public init(west: Double, south: Double, east: Double, north: Double) throws {
    guard [west, south, east, north].allSatisfy(\.isFinite), west >= -180, east <= 180,
      south >= -85, north <= 85, west < east, south < north,
      east - west <= 10, north - south <= 10
    else { throw SentinelError.region }
    self.west = west
    self.south = south
    self.east = east
    self.north = north
  }
  public init(corners a: Coordinate, _ b: Coordinate) throws {
    try self.init(
      west: min(a.longitude, b.longitude), south: min(a.latitude, b.latitude),
      east: max(a.longitude, b.longitude), north: max(a.latitude, b.latitude))
  }
  public init(from decoder: any Decoder) throws {
    let c = try decoder.container(keyedBy: CodingKeys.self)
    try self.init(
      west: c.decode(Double.self, forKey: .west), south: c.decode(Double.self, forKey: .south),
      east: c.decode(Double.self, forKey: .east), north: c.decode(Double.self, forKey: .north))
  }
  public var outline: [Coordinate] {
    [(south, west), (south, east), (north, east), (north, west), (south, west)].compactMap {
      Coordinate(latitude: $0.0, longitude: $0.1)
    }
  }
  public var wkt: String {
    "POLYGON((\(west) \(south),\(east) \(south),\(east) \(north),\(west) \(north),\(west) \(south)))"
  }
  public var summary: String { String(format: "%.4f, %.4f → %.4f, %.4f", south, west, north, east) }
}

public enum SentinelPolarization: String, CaseIterable, Codable, Sendable {
  case both = "VV + VH"
  case vv = "VV"
  case vh = "VH"
  var nameFilter: String {
    switch self {
    case .both, .vh: "contains(Name,'_1SDV_')"
    case .vv: "(contains(Name,'_1SDV_') or contains(Name,'_1SSV_'))"
    }
  }
}
public struct SentinelQuery: Codable, Equatable, Sendable {
  public let region: SentinelRegion
  public let start: Date
  public let end: Date
  public let polarization: SentinelPolarization
  public let mode: String
  public init(
    region: SentinelRegion, start: Date, end: Date,
    polarization: SentinelPolarization, mode: String = "IW"
  ) throws {
    guard ["IW", "EW", "SM"].contains(mode), start <= end,
      end.timeIntervalSince(start) <= 366 * 86_400,
      start.timeIntervalSince1970.isFinite, end.timeIntervalSince1970.isFinite
    else { throw SentinelError.query }
    self.region = region
    self.start = start
    self.end = end
    self.polarization = polarization
    self.mode = mode
  }
  public func url(skip: Int = 0) throws -> URL {
    guard (0...980).contains(skip), skip % 20 == 0 else { throw SentinelError.query }
    let formatter = ISO8601DateFormatter()
    var calendar = Calendar(identifier: .gregorian)
    calendar.timeZone = TimeZone(secondsFromGMT: 0)!
    let first = calendar.startOfDay(for: start)
    let afterLast = calendar.date(byAdding: .day, value: 1, to: calendar.startOfDay(for: end))!
    // Stripmap products encode the beam S1–S6 in their SAFE names, rather than SM.
    let modes = mode == "SM" ? ["SM", "S1", "S2", "S3", "S4", "S5", "S6"] : [mode]
    let modeFilter =
      "(" + modes.map { "contains(Name,'_\($0)_GRD')" }.joined(separator: " or ") + ")"
    let filter =
      "Collection/Name eq 'SENTINEL-1' and \(modeFilter) and not contains(Name,'_COG') and \(polarization.nameFilter) and ContentDate/Start ge \(formatter.string(from: first)) and ContentDate/Start lt \(formatter.string(from: afterLast)) and OData.CSC.Intersects(area=geography'SRID=4326;\(region.wkt)')"
    var url = URLComponents(string: "https://catalogue.dataspace.copernicus.eu/odata/v1/Products")!
    url.queryItems = [
      .init(name: "$filter", value: filter),
      .init(name: "$orderby", value: "ContentDate/Start desc,Id"),
      .init(name: "$top", value: "20"), .init(name: "$skip", value: String(skip)),
      .init(name: "$expand", value: "Attributes"),
    ]
    return url.url!
  }
}
public struct SentinelScene: Codable, Identifiable, Equatable, Sendable {
  public let id: UUID
  public let name: String
  public let acquisition: String
  public let contentLength: Int64
  public let online: Bool
  public let orbit: String
  public let relativeOrbit: Int?
  public let footprint: [[Coordinate]]
  /// The admitted official product name includes acquisition times and a unique product suffix.
  public var archiveFilename: String {
    Self.supportedName(name)
      ? String(name.dropLast(5)) + ".zip" : id.uuidString.lowercased() + ".zip"
  }
  public var satellite: String { String(name.prefix(3)) }
  public var polarizations: String { name.contains("_1SDV_") ? "VV + VH" : "VV" }
  public var mode: String {
    let beam = name.split(separator: "_").dropFirst().first.map(String.init) ?? ""
    return ["S1", "S2", "S3", "S4", "S5", "S6"].contains(beam) ? "SM" : beam
  }
  public var downloadURL: URL {
    URL(
      string:
        "https://download.dataspace.copernicus.eu/odata/v1/Products(\(id.uuidString.lowercased()))/$value"
    )!
  }
  public static func supportedName(_ name: String) -> Bool {
    name.count <= 200
      && name.range(
        of:
          "^S1[A-D]_(IW|EW|SM|S[1-6])_GRD[HMF]_1S(DV|SV)_[0-9]{8}T[0-9]{6}_[0-9]{8}T[0-9]{6}_[0-9]{6}_[A-Fa-f0-9]{6}_[A-Fa-f0-9]{4}\\.SAFE$",
        options: .regularExpression) != nil
  }
}
public struct SentinelPage: Sendable {
  public let scenes: [SentinelScene]
  public let hasMore: Bool
  public let rejected: Int
  public static func parse(_ data: Data) throws -> SentinelPage {
    guard let root = try JSONSerialization.jsonObject(with: data) as? [String: Any],
      let rows = root["value"] as? [[String: Any]], rows.count <= 20
    else { throw SentinelError.catalogue }
    var seen = Set<UUID>()
    let scenes = rows.compactMap { row -> SentinelScene? in
      guard let raw = row["Id"] as? String, let id = UUID(uuidString: raw),
        seen.insert(id).inserted,
        let name = row["Name"] as? String, SentinelScene.supportedName(name),
        let length = row["ContentLength"] as? NSNumber, length.int64Value > 0,
        length.int64Value <= SentinelDownload.maximumBytes,
        let online = row["Online"] as? Bool,
        let date = (row["ContentDate"] as? [String: Any])?["Start"] as? String, date.count <= 40,
        ProviderParsers.date(date) != nil
      else { return nil }
      var orbit = "Unknown orbit"
      var relative: Int?
      for attribute in (row["Attributes"] as? [[String: Any]] ?? []).prefix(100) {
        if attribute["Name"] as? String == "orbitDirection",
          let value = attribute["Value"] as? String,
          ["ASCENDING", "DESCENDING"].contains(value)
        {
          orbit = value
        }
        if attribute["Name"] as? String == "relativeOrbitNumber",
          let value = attribute["Value"] as? Int,
          (1...175).contains(value)
        {
          relative = value
        }
      }
      return SentinelScene(
        id: id, name: name, acquisition: date, contentLength: length.int64Value,
        online: online, orbit: orbit, relativeOrbit: relative,
        footprint: ReferenceLayers.geometry(row["GeoFootprint"]))
    }
    return SentinelPage(
      scenes: scenes, hasMore: rows.count == 20, rejected: rows.count - scenes.count)
  }
}
public enum SentinelError: Error, LocalizedError {
  case region, query, catalogue, authentication, signedOut
  case download(Int)
  case oversized, invalidArchive, disk, redirect
  public var errorDescription: String? {
    switch self {
    case .region:
      "Choose a rectangle within ±85° latitude, up to 10° wide/high. It must not cross the antimeridian."
    case .query: "Choose a valid date range of at most one year and an available acquisition mode."
    case .catalogue: "Copernicus returned an unsupported catalogue response. Try the search again."
    case .authentication:
      "Copernicus sign-in failed. Check your account details and, if enabled, your current two-factor code."
    case .signedOut: "Sign in to your free Copernicus Data Space account to download a product."
    case .download(let code):
      switch code {
      case 401, 403:
        "Copernicus rejected download authorization. Sign in again and check your account access."
      case 429: "Copernicus download quota reached. Wait before trying again."
      case 404: "This acquisition is no longer available for download. Search again."
      default: "Copernicus download failed (HTTP \(code)). Try again later."
      }
    case .oversized: "The download exceeds the supported 40 GiB size limit."
    case .invalidArchive:
      "The download is incomplete or is not a ZIP product. No partial dataset was retained."
    case .disk: "Not enough free disk space for the download. Free space and try again."
    case .redirect:
      "The download redirected outside the trusted Copernicus service. Authorization was not forwarded."
    }
  }
}
public struct SentinelCatalog: Sendable {
  private let transport: Transport
  public init(transport: Transport = Transport()) { self.transport = transport }
  public func search(_ query: SentinelQuery, skip: Int = 0) async throws -> SentinelPage {
    try SentinelPage.parse(
      await transport.data(query.url(skip: skip), maximumBytes: 2 * 1_024 * 1_024))
  }
}
/// Passwords and tokens stay in memory. No credentials enter jobs, logs, or workspace exports.
public actor CopernicusAccount {
  private struct Token: Decodable {
    let accessToken: String
    let refreshToken: String?
    let expiresIn: Double
    enum CodingKeys: String, CodingKey {
      case accessToken = "access_token"
      case refreshToken = "refresh_token"
      case expiresIn = "expires_in"
    }
  }
  private let transport: Transport
  private var token: Token?
  private var expires = Date.distantPast
  private var generation = UUID()
  public init(transport: Transport = Transport()) { self.transport = transport }
  public func signIn(username: String, password: String, totp: String = "") async throws {
    generation = UUID()
    let generation = generation
    token = nil
    guard !username.isEmpty, !password.isEmpty, username.count <= 320, password.count <= 1024,
      totp.isEmpty || (totp.count == 6 && totp.allSatisfy(\.isNumber))
    else { throw SentinelError.authentication }
    var form = [
      "client_id": "cdse-public", "grant_type": "password", "username": username,
      "password": password,
    ]
    if !totp.isEmpty { form["totp"] = totp }
    try await request(form, generation: generation)
  }
  public func signOut() {
    generation = UUID()
    token = nil
    expires = .distantPast
  }
  public func accessToken() async throws -> String {
    guard let token else { throw SentinelError.signedOut }
    let currentGeneration = generation
    if Date() >= expires {
      guard let refresh = token.refreshToken else { throw SentinelError.signedOut }
      do {
        try await request(
          [
            "client_id": "cdse-public", "grant_type": "refresh_token", "refresh_token": refresh,
          ], generation: currentGeneration)
      } catch {
        if generation == currentGeneration { self.token = nil }
        throw error
      }
    }
    guard let access = self.token?.accessToken else { throw SentinelError.signedOut }
    return access
  }
  private func request(_ form: [String: String], generation: UUID) async throws {
    do {
      let data = try await transport.data(
        URL(
          string:
            "https://identity.dataspace.copernicus.eu/auth/realms/CDSE/protocol/openid-connect/token"
        )!,
        maximumBytes: 64 * 1024, allowRedirects: false, form: form)
      let value = try JSONDecoder().decode(Token.self, from: data)
      guard !value.accessToken.isEmpty, value.accessToken.count <= 32_768,
        value.expiresIn.isFinite, value.expiresIn > 0
      else { throw SentinelError.authentication }
      try Task.checkCancellation()
      guard generation == self.generation else { throw CancellationError() }
      token = value
      expires = Date().addingTimeInterval(max(0, value.expiresIn - 30))
    } catch is CancellationError { throw CancellationError() } catch {
      throw SentinelError.authentication
    }
  }
}

public struct SentinelDownloadProgress: Sendable {
  public let received: Int64
  public let expected: Int64?
  public var fraction: Double? {
    expected.flatMap { $0 > 0 ? min(1, Double(received) / Double($0)) : nil }
  }
}
/// Streaming URLSession delegate; writes network chunks directly to disk off the main actor.
/// Its session uses one serial delegate queue. The lock only guards start/cancellation races.
public final class SentinelDownload: NSObject, URLSessionDataDelegate, @unchecked Sendable {
  public static let maximumBytes: Int64 = 40 * 1024 * 1024 * 1024
  private let destination: URL
  private let partial: URL
  private let maximum: Int64
  private let progress: @Sendable (SentinelDownloadProgress) -> Void
  private let lock = NSLock()
  private var cancelled = false
  private var task: URLSessionDataTask?
  private var session: URLSession?
  private var continuation: CheckedContinuation<URL, any Error>?
  private var file: FileHandle?
  private var received: Int64 = 0
  private var expected: Int64?
  private var failure: (any Error)?
  private var lastProgress = Date.distantPast
  public init(
    destination: URL, maximumBytes: Int64 = SentinelDownload.maximumBytes,
    progress: @escaping @Sendable (SentinelDownloadProgress) -> Void
  ) {
    self.destination = destination
    partial = destination.deletingLastPathComponent().appendingPathComponent(
      ".partial-\(ProcessInfo.processInfo.processIdentifier)-\(UUID().uuidString)")
    maximum = maximumBytes
    self.progress = progress
  }
  public func run(
    scene: SentinelScene, token: String,
    configuration: URLSessionConfiguration = .ephemeral
  ) async throws -> URL {
    try Task.checkCancellation()
    guard SentinelScene.supportedName(scene.name) else { throw SentinelError.catalogue }
    guard scene.online else { throw SentinelError.download(404) }
    guard scene.contentLength > 0, scene.contentLength <= Self.maximumBytes else {
      throw SentinelError.oversized
    }
    try FileManager.default.createDirectory(
      at: destination.deletingLastPathComponent(), withIntermediateDirectories: true)
    let capacity = try destination.deletingLastPathComponent().resourceValues(forKeys: [
      .volumeAvailableCapacityForImportantUsageKey
    ]).volumeAvailableCapacityForImportantUsage
    if let capacity,
      capacity < max(2 * 1024 * 1024 * 1024, scene.contentLength + 1024 * 1024 * 1024)
    {
      throw SentinelError.disk
    }
    // A completed dataset is never silently replaced by a retry.
    guard !FileManager.default.fileExists(atPath: destination.path) else {
      throw CocoaError(.fileWriteFileExists)
    }
    try Data().write(to: partial, options: .withoutOverwriting)
    defer {
      try? file?.close()
      try? FileManager.default.removeItem(at: partial)
    }
    file = try FileHandle(forWritingTo: partial)
    configuration.urlCache = nil
    configuration.httpCookieStorage = nil
    configuration.timeoutIntervalForRequest = 120
    configuration.timeoutIntervalForResource = 6 * 3600
    let queue = OperationQueue()
    queue.maxConcurrentOperationCount = 1
    let session = URLSession(configuration: configuration, delegate: self, delegateQueue: queue)
    self.session = session
    defer {
      session.invalidateAndCancel()
      self.session = nil
    }
    var request = URLRequest(url: scene.downloadURL)
    request.setValue("Bearer \(token)", forHTTPHeaderField: "Authorization")
    request.setValue("HeliosNative/0.1", forHTTPHeaderField: "User-Agent")
    // Disable content encoding so response length describes the bytes written to the ZIP.
    request.setValue("identity", forHTTPHeaderField: "Accept-Encoding")
    return try await withTaskCancellationHandler {
      try await withCheckedThrowingContinuation { continuation in
        self.continuation = continuation
        let task = session.dataTask(with: request)
        let shouldCancel = lock.withLock {
          self.task = task
          return cancelled
        }
        task.resume()
        if shouldCancel { task.cancel() }
      }
    } onCancel: {
      self.cancel()
    }
  }
  public func cancel() {
    let active = lock.withLock {
      cancelled = true
      return task
    }
    active?.cancel()
  }
  public static func trusted(_ url: URL?) -> Bool {
    guard let url, url.scheme == "https", url.user == nil, url.password == nil,
      url.port == nil || url.port == 443, let host = url.host?.lowercased()
    else { return false }
    return host == "download.dataspace.copernicus.eu" || host == "catalogue.dataspace.copernicus.eu"
  }
  public func urlSession(
    _ session: URLSession, task: URLSessionTask,
    willPerformHTTPRedirection response: HTTPURLResponse, newRequest request: URLRequest,
    completionHandler: @escaping @Sendable (URLRequest?) -> Void
  ) {
    guard Self.trusted(request.url) else {
      failure = SentinelError.redirect
      completionHandler(nil)
      return
    }
    var redirected = request
    redirected.setValue(
      task.originalRequest?.value(forHTTPHeaderField: "Authorization"),
      forHTTPHeaderField: "Authorization")
    completionHandler(redirected)
  }
  public func urlSession(
    _ session: URLSession, dataTask: URLSessionDataTask, didReceive response: URLResponse,
    completionHandler: @escaping @Sendable (URLSession.ResponseDisposition) -> Void
  ) {
    guard failure == nil, Self.trusted(response.url), let response = response as? HTTPURLResponse
    else {
      failure = failure ?? SentinelError.redirect
      completionHandler(.cancel)
      return
    }
    guard response.statusCode == 200 else {
      failure = SentinelError.download(response.statusCode)
      completionHandler(.cancel)
      return
    }
    guard response.expectedContentLength <= maximum else {
      failure = SentinelError.oversized
      completionHandler(.cancel)
      return
    }
    expected = response.expectedContentLength > 0 ? response.expectedContentLength : nil
    completionHandler(.allow)
  }
  public func urlSession(_ session: URLSession, dataTask: URLSessionDataTask, didReceive data: Data)
  {
    guard failure == nil else { return }
    guard Int64(data.count) <= maximum - received else {
      failure = SentinelError.oversized
      dataTask.cancel()
      return
    }
    do {
      try file?.write(contentsOf: data)
      received += Int64(data.count)
      if Date().timeIntervalSince(lastProgress) >= 0.25 {
        lastProgress = Date()
        progress(.init(received: received, expected: expected))
      }
    } catch {
      failure = SentinelError.disk
      dataTask.cancel()
    }
  }
  public func urlSession(
    _ session: URLSession, task: URLSessionTask, didCompleteWithError error: (any Error)?
  ) {
    let completion = continuation
    continuation = nil
    do {
      try file?.close()
      file = nil
      if lock.withLock({ cancelled }) { throw CancellationError() }
      if let failure { throw failure }
      if let error { throw error }
      guard expected == nil || expected == received else { throw SentinelError.invalidArchive }
      try Self.validateArchive(partial)
      try FileManager.default.moveItem(at: partial, to: destination)
      progress(.init(received: received, expected: received))
      completion?.resume(returning: destination)
    } catch { completion?.resume(throwing: error) }
  }
  public static func validateArchive(_ url: URL) throws {
    let handle = try FileHandle(forReadingFrom: url)
    defer { try? handle.close() }
    guard try handle.read(upToCount: 4) == Data([0x50, 0x4b, 0x03, 0x04]) else {
      throw SentinelError.invalidArchive
    }
    let size = try handle.seekToEnd()
    guard size >= 22 else { throw SentinelError.invalidArchive }
    try handle.seek(toOffset: size > 65_557 ? size - 65_557 : 0)
    let tail = try handle.readToEnd() ?? Data()
    guard tail.range(of: Data([0x50, 0x4b, 0x05, 0x06]), options: .backwards) != nil else {
      throw SentinelError.invalidArchive
    }
    // Full ZIP CRC, path, SAFE/XML and measurement admission remains in the existing SAR inspector.
  }
}
public struct SentinelDownloadRecord: Codable, Identifiable, Sendable {
  public var id: UUID { scene.id }
  public let scene: SentinelScene
  public let query: SentinelQuery
  public let downloadedAt: Date
  public init(scene: SentinelScene, query: SentinelQuery, downloadedAt: Date = Date()) {
    self.scene = scene
    self.query = query
    self.downloadedAt = downloadedAt
  }
}

extension SentinelDownloadRecord {
  /// Rename in place: no satellite data are copied, and an existing destination is never overwritten.
  public func archive(in directory: URL, migrateLegacy: Bool = true) throws -> URL? {
    guard SentinelScene.supportedName(scene.name),
      UUID(uuidString: directory.lastPathComponent) == id,
      (try? directory.resourceValues(forKeys: [.isSymbolicLinkKey]).isSymbolicLink) == false
    else { return nil }
    func regular(_ file: URL) -> Bool {
      guard let values = try? file.resourceValues(forKeys: [.isSymbolicLinkKey, .isRegularFileKey])
      else { return false }
      return values.isSymbolicLink == false && values.isRegularFile == true
    }
    let preferred = directory.appendingPathComponent(scene.archiveFilename)
    if FileManager.default.fileExists(atPath: preferred.path) {
      return regular(preferred) ? preferred : nil
    }
    let legacy = directory.appendingPathComponent("product.zip")
    guard regular(legacy) else { return nil }
    guard migrateLegacy else { return legacy }
    try FileManager.default.moveItem(at: legacy, to: preferred)
    return preferred
  }
}
