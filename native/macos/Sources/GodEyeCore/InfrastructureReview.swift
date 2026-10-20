import Foundation

public struct InfrastructureSite: Codable, Identifiable, Equatable, Sendable {
  public var id: UUID
  public var name: String
  public var kind: String
  public var region: SentinelRegion
  public var createdAt: Date
  public init(name: String, kind: String, region: SentinelRegion) {
    id = UUID()
    self.name = name.trimmingCharacters(in: .whitespacesAndNewlines)
    self.kind = kind
    self.region = region
    createdAt = Date(timeIntervalSince1970: Date().timeIntervalSince1970.rounded(.down))
  }
  public var isValid: Bool {
    !name.isEmpty && name.count <= 120 && kind.count <= 80
      && createdAt.timeIntervalSince1970.isFinite
  }
}
public struct ChangeReviewOptions: Codable, Sendable {
  public var polarization = "VV"
  public var thresholdDb = 3.0
  public var averagingWindow = 5
  public var minimumPixels = 25
  public init() {}
  public var isValid: Bool {
    ["VV", "VH"].contains(polarization) && thresholdDb.isFinite && (1...20).contains(thresholdDb)
      && [3, 5, 7].contains(averagingWindow) && (9...10_000).contains(minimumPixels)
  }
}
public struct ChangeReviewRequest: Codable, Sendable {
  public var site: InfrastructureSite
  public var options: ChangeReviewOptions
  public var beforeDirectory: String
  public var afterDirectory: String
  public init(site: InfrastructureSite, options: ChangeReviewOptions, before: URL, after: URL) {
    self.site = site
    self.options = options
    beforeDirectory = before.path
    afterDirectory = after.path
  }
}
public struct ChangeCandidate: Codable, Identifiable, Sendable {
  public var id: String
  public var direction: String
  public var bounds: [Double]
  public var pixels: Int
  public var approximateAreaM2: Double
  public var meanDeltaDb: Double
  public var maximumAbsoluteDeltaDb: Double
  public var isValid: Bool {
    (Int(id).map { (1...500).contains($0) } ?? false)
      && ["increase", "decrease"].contains(direction) && ChangeReviewResult.validBounds(bounds)
      && pixels >= 9 && approximateAreaM2.isFinite && approximateAreaM2 > 0
      && meanDeltaDb.isFinite && maximumAbsoluteDeltaDb.isFinite
      && maximumAbsoluteDeltaDb >= abs(meanDeltaDb) - 0.0001
  }
}
public struct ReviewEvidence: Codable, Sendable {
  public var jobID: String
  public var product: String
  public var metadata: SarProduct
  public var options: SarOptions
  public var metadataSHA256: String
  public var alignedSitePowerSHA256: String
  public var scientificBytes: Int64
  public var scientific: String
  public var processingStages: [String]
  public var engine: [String: String]
  public var output: SarOutput?
}
public struct ChangeReviewResult: Codable, Sendable {
  public var version: Int
  public var workerPID: Int32?
  public var createdAt: String
  public var state: SarState
  public var error: String?
  public var site: InfrastructureSite?
  public var options: ChangeReviewOptions?
  public var evidence: [ReviewEvidence]?
  public var candidates: [ChangeCandidate]?
  public var bounds: [Double]?
  public var width: Int?
  public var height: Int?
  public var commonValidFraction: Double?
  public var sharedDisplayRangeDb: [Double]?
  public var completedAt: String?
  public var elapsedSeconds: Double?
  public var quantity: String?
  public var alignment: String?
  public var crs: String?
  public var changeDisplayRangeDb: [Double]?
  public var limitations: [String]?
  public var analysisStatus: String?
  public var quality: [String: PortableJSONValue]?
  public var sourceMetadataSHA256: [String]?
  public var isValid: Bool {
    guard version == 1 else { return false }
    guard state == .completed else { return true }
    guard let site, site.isValid, let options, options.isValid,
      let evidence, evidence.count == 2,
      evidence.allSatisfy({
        UUID(uuidString: $0.jobID) != nil && $0.metadataSHA256.count == 64
          && $0.metadataSHA256.allSatisfy { $0.isHexDigit }
          && $0.alignedSitePowerSHA256.count == 64
          && $0.alignedSitePowerSHA256.allSatisfy { $0.isHexDigit }
          && $0.scientificBytes > 0
      }),
      let candidates, candidates.count <= 500, candidates.allSatisfy(\.isValid),
      Set(candidates.map(\.id)).count == candidates.count,
      let bounds, Self.validBounds(bounds), let width, let height,
      width > 0 && height > 0 && width <= 1_000_000 / height,
      let fraction = commonValidFraction, fraction.isFinite && (0.5...1).contains(fraction),
      let range = sharedDisplayRangeDb, range.count == 2,
      range.allSatisfy(\.isFinite) && range[0] < range[1]
    else { return false }
    return true
  }
  public static func validBounds(_ b: [Double]) -> Bool {
    b.count == 4 && b.allSatisfy(\.isFinite) && b[0] >= -180 && b[2] <= 180
      && b[0] < b[2] && b[1] >= -85 && b[3] <= 85 && b[1] < b[3] && b[2] - b[0] <= 10
  }
}

/// An explicit developer validation profile keeps generated QA fixtures away
/// from the user's acquisitions/sites. Production always defaults to App Support.
public enum SatelliteStorage {
  public static var defaultRoot: URL {
    FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask)[0]
      .appendingPathComponent("GodEye", isDirectory: true)
  }
  public static var root: URL {
    if let value = ProcessInfo.processInfo.environment["GODEYE_VALIDATION_ROOT"],
      value.hasPrefix("/"), !value.contains("\0")
    {
      return URL(fileURLWithPath: value, isDirectory: true)
    }
    return defaultRoot
  }
}
public enum ChangeDecision: String, Codable, CaseIterable, Sendable {
  case unreviewed = "Unreviewed"
  case followUp = "Follow up"
  case explained = "Explained / dismiss"
  case uncertain = "Uncertain"
}
public struct CandidateReview: Codable, Sendable {
  public var decision = ChangeDecision.unreviewed
  public var note = ""
  /// Analyst-supplied URLs/document citations, never invented automated evidence.
  public var reference = ""
  public var updatedAt = Date()
  public init() {}
  public var isValid: Bool { note.count <= 4000 && reference.count <= 1000 }
}
public struct ChangeReviewNotes: Codable, Sendable {
  public var version = 1
  public var reviewID: UUID
  public var candidates: [String: CandidateReview] = [:]
  public init(reviewID: UUID) { self.reviewID = reviewID }
  public var isValid: Bool {
    version == 1 && candidates.count <= 500 && candidates.values.allSatisfy(\.isValid)
      && candidates.keys.allSatisfy { Int($0).map { (1...500).contains($0) } ?? false }
  }
}

/// UI preflight mirrors the worker's authoritative checks. No acquisition order
/// or geometry mismatch is silently interpreted as a physical change.
public enum ChangeReviewCompatibility {
  public static func issue(before: SarResult, after: SarResult, polarization: String) -> String? {
    guard before.state == .completed, after.state == .completed,
      let a = before.metadata, let b = after.metadata,
      let ao = before.options, let bo = after.options
    else { return "Choose two completed SAR results." }
    func date(_ value: String) -> Date? {
      ProviderParsers.date(value) ?? ProviderParsers.date(value + "Z")
    }
    guard let first = date(a.acquisitionStart), let last = date(b.acquisitionStart), first < last
    else { return "The later acquisition must be newer than the baseline." }
    guard a.productType == "GRD", b.productType == "GRD", a.mission == b.mission,
      a.satellite == b.satellite, a.mode == b.mode, a.orbitDirection == b.orbitDirection,
      ["ASCENDING", "DESCENDING"].contains(a.orbitDirection),
      let orbit = a.relativeOrbit, (1...175).contains(orbit), orbit == b.relativeOrbit
    else { return "Use the same satellite, mode, orbit direction and known relative orbit." }
    guard ao.applyOrbit == bo.applyOrbit, ao.speckle == bo.speckle,
      ao.pixelSpacing == bo.pixelSpacing
    else { return "Both acquisitions need matching orbit, speckle and output-spacing settings." }
    guard ["VV", "VH"].contains(polarization),
      [before, after].allSatisfy({
        $0.outputs.contains { $0.id == polarization && $0.scientific != nil && $0.hasValidBounds }
      })
    else { return "Both acquisitions need a calibrated output for this polarization." }
    return nil
  }
}

/// Small atomic JSON documents follow the native annotation storage convention.
public enum SatelliteDocuments {
  public static func loadSites(from url: URL) throws -> [InfrastructureSite] {
    guard FileManager.default.fileExists(atPath: url.path) else { return [] }
    let sites = try SarProcessor.read([InfrastructureSite].self, from: url)
    guard sites.count <= 500, sites.allSatisfy(\.isValid), Set(sites.map(\.id)).count == sites.count
    else { throw FeedError.malformed }
    return sites
  }
  public static func saveSites(_ sites: [InfrastructureSite], to url: URL) throws {
    guard sites.count <= 500, sites.allSatisfy(\.isValid), Set(sites.map(\.id)).count == sites.count
    else { throw FeedError.malformed }
    // Preserve an unreadable previous document instead of silently overwriting it.
    _ = try loadSites(from: url)
    try save(sites, to: url)
  }
  /// Remove only the saved boundary. Reviews retain their original site snapshot.
  public static func deleteSite(id: UUID, from url: URL) throws -> [InfrastructureSite] {
    let sites = try loadSites(from: url)
    guard sites.contains(where: { $0.id == id }) else { throw FeedError.malformed }
    let remaining = sites.filter { $0.id != id }
    try saveSites(remaining, to: url)
    return remaining
  }
  /// Derive the directory from its ID, never from an imported product or output path.
  /// A failed move leaves the image in the library; no permanent-delete fallback.
  public static func trashOpticalImage(
    id: UUID, root: URL,
    moveToTrash: (URL) throws -> Void = { url in
      try FileManager.default.trashItem(at: url, resultingItemURL: nil)
    }
  ) throws {
    let directory = root.appendingPathComponent(id.uuidString, isDirectory: true)
    let attributes = try directory.resourceValues(forKeys: [.isDirectoryKey, .isSymbolicLinkKey])
    guard attributes.isDirectory == true, attributes.isSymbolicLink != true,
      directory.resolvingSymlinksInPath().deletingLastPathComponent().standardizedFileURL
        == root.resolvingSymlinksInPath().standardizedFileURL
    else { throw FeedError.malformed }
    let result = try SarProcessor.read(
      SentinelOpticalResult.self, from: directory.appendingPathComponent("result.json"))
    guard result.isValid, result.state == .completed else { throw FeedError.malformed }
    try moveToTrash(directory)
  }
  public static func save<T: Encodable>(
    _ value: T, to url: URL, dateEncodingStrategy: JSONEncoder.DateEncodingStrategy = .iso8601,
    maximumBytes: Int = 1024 * 1024
  ) throws {
    let encoder = JSONEncoder()
    encoder.outputFormatting = [.prettyPrinted, .sortedKeys]
    encoder.dateEncodingStrategy = dateEncodingStrategy
    let data = try encoder.encode(value)
    guard (1...8 * 1024 * 1024).contains(maximumBytes), data.count <= maximumBytes else {
      throw FeedError.oversized
    }
    try FileManager.default.createDirectory(
      at: url.deletingLastPathComponent(), withIntermediateDirectories: true)
    try data.write(to: url, options: .atomic)
  }
}
