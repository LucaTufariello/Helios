import Darwin
import Foundation

public struct InvestigationCitation: Codable, Equatable, Sendable, Identifiable {
  public var id: UUID
  public var label: String
  public var sourceID: String
  public var provider: String
  public var reference: String
  public var qualityLimitations: String
  public var fingerprint: String
  public var capturedAt: Date
  public var sourceDate: PortableJSONValue?
  public var derivationReferences: [String]
  public var evidence: [String: PortableJSONValue]?
  public var origin: [String: PortableJSONValue]?
  private var validSourceDate: Bool {
    switch sourceDate {
    case .none, .some(.null): return true
    case .some(.number(let n)): return n.isFinite
    case .some(.string(let s)): return ProviderParsers.date(s) != nil
    default: return false
    }
  }
  public var isValid: Bool {
    validSourceDate && !label.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty
      && label.count <= 1024
      && !sourceID.isEmpty && sourceID.count <= 1024 && provider.count <= 512
      && reference.count <= 4096 && qualityLimitations.count <= 16000 && fingerprint.count <= 256
      && capturedAt.timeIntervalSince1970.isFinite && derivationReferences.count <= 100
      && derivationReferences.allSatisfy { $0.count <= 4096 }
      && ((try? JSONEncoder().encode(self).count).map { $0 <= 128 * 1024 } ?? false)
  }
}
public struct Asset: Codable, Equatable, Sendable, Identifiable {
  public var id: UUID
  public var name: String
  public var category: String
  public var aliases: [String]
  public var location: Coordinate?
  public var boundary: SentinelRegion?
  public var identifiers: [String: String]
  public var citations: [InvestigationCitation]
  public var revision: Int
  public var archived: Bool
  public var isValid: Bool {
    !name.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty && name.count <= 240
      && !category.isEmpty && category.count <= 120 && aliases.count <= 50
      && aliases.allSatisfy { $0.count <= 240 } && (location != nil || boundary != nil)
      && identifiers.count <= 50
      && identifiers.allSatisfy {
        !$0.key.isEmpty && $0.key.count <= 512 && !$0.value.isEmpty && $0.value.count <= 1024
      }
      && (1...1_000_000).contains(revision) && citations.count <= 500
      && Set(citations.map(\.id)).count == citations.count && citations.allSatisfy(\.isValid)
  }
}
public struct AssetRelationship: Codable, Equatable, Sendable, Identifiable {
  public var id: UUID
  public var fromAssetID: UUID
  public var toAssetID: UUID
  public var kind: String
  public var reviewState: String
  public var rationale: String
  public var validFrom: String
  public var validThrough: String
  public var citations: [InvestigationCitation]
  public var revision: Int
  public var archived: Bool
  public var isValid: Bool {
    func date(_ s: String) -> Bool {
      if s.isEmpty { return true }
      let f = DateFormatter()
      f.locale = Locale(identifier: "en_US_POSIX")
      f.timeZone = TimeZone(secondsFromGMT: 0)
      f.dateFormat = "yyyy-MM-dd"
      f.isLenient = false
      return s.count == 10 && f.date(from: s).map { f.string(from: $0) == s } == true
    }
    return fromAssetID != toAssetID && ["Part of", "Connected to", "Depends on"].contains(kind)
      && ["Unreviewed", "Supported", "Uncertain", "Dismissed"].contains(reviewState)
      && rationale.count <= 16000 && date(validFrom) && date(validThrough)
      && (validFrom.isEmpty || validThrough.isEmpty || validFrom <= validThrough)
      && (1...1_000_000).contains(revision) && citations.count <= 500
      && Set(citations.map(\.id)).count == citations.count && citations.allSatisfy(\.isValid)
      && (kind != "Depends on" || !citations.isEmpty)
      && (reviewState == "Unreviewed"
        || (!citations.isEmpty
          && !rationale.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty))
  }
}
public struct InvestigationSnapshot: Codable, Equatable, Sendable, Identifiable {
  public var id: UUID
  public var capturedAt: Date
  public var areaID: UUID?
  public var asset: Asset?
  public var relationship: AssetRelationship?
  public var isValid: Bool {
    capturedAt.timeIntervalSince1970.isFinite && ((asset != nil) != (relationship != nil))
      && (asset?.isValid ?? true) && (relationship?.isValid ?? true)
  }
}
public struct FindingCitation: Codable, Equatable, Sendable {
  public var citationID: UUID
  public var role: String
}
public struct FindingReviewRevision: Codable, Equatable, Sendable {
  public var id: UUID
  public var claim: String
  public var timeContext: String
  public var alternatives: String
  public var rationale: String
  public var state: String
  public var assetSnapshotIDs: [UUID]
  public var areaIDs: [UUID]
  public var areaSnapshots: [MissionArea]?
  public var citations: [FindingCitation]
  public var origin: [String: PortableJSONValue]
  public var revision: Int
  public var createdAt: Date
  public var recordedAt: Date
  public func isValid(snapshots: Set<UUID>, sources: Set<UUID>) -> Bool {
    validFinding(
      claim: claim, timeContext: timeContext, alternatives: alternatives, rationale: rationale,
      state: state, assetIDs: assetSnapshotIDs, areaIDs: areaIDs, citations: citations,
      snapshots: snapshots, sources: sources)
      && (areaSnapshots ?? []).allSatisfy { areaIDs.contains($0.id) && $0.isValid }
      && Set((areaSnapshots ?? []).map(\.id)).count == (areaSnapshots ?? []).count
      && (areaSnapshots?.count ?? 0) <= 100
      && (1...1_000_000).contains(revision) && createdAt.timeIntervalSince1970.isFinite
      && recordedAt.timeIntervalSince1970.isFinite
  }
}
private func validFinding(
  claim: String, timeContext: String, alternatives: String, rationale: String,
  state: String, assetIDs: [UUID], areaIDs: [UUID], citations: [FindingCitation],
  snapshots: Set<UUID>, sources: Set<UUID>
) -> Bool {
  !claim.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty && claim.count <= 16000
    && timeContext.count <= 4000 && alternatives.count <= 16000 && rationale.count <= 16000
    && ["Unreviewed", "Supported", "Uncertain", "Dismissed"].contains(state)
    && assetIDs.count <= 100 && Set(assetIDs).count == assetIDs.count
    && Set(assetIDs).isSubset(of: snapshots)
    && areaIDs.count <= 100 && citations.count <= 500
    && Set(citations.map(\.citationID)).count == citations.count
    && citations.allSatisfy {
      sources.contains($0.citationID)
        && ["Supporting", "Contradicting", "Context"].contains($0.role)
    }
    && (state == "Unreviewed" || !rationale.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty)
    && (state != "Supported" || citations.contains { $0.role == "Supporting" })
}
public struct Finding: Codable, Equatable, Sendable, Identifiable {
  public var id: UUID
  public var claim: String
  public var timeContext: String
  public var alternatives: String
  public var rationale: String
  public var state: String
  public var assetSnapshotIDs: [UUID]
  public var areaIDs: [UUID]
  public var areaSnapshots: [MissionArea]?
  public var citations: [FindingCitation]
  public var revisions: [FindingReviewRevision]
  public var origin: [String: PortableJSONValue]
  public var revision: Int
  public var createdAt: Date
  public func isValid(snapshots: Set<UUID>, sources: Set<UUID>) -> Bool {
    validFinding(
      claim: claim, timeContext: timeContext, alternatives: alternatives, rationale: rationale,
      state: state, assetIDs: assetSnapshotIDs, areaIDs: areaIDs, citations: citations,
      snapshots: snapshots, sources: sources)
      && (areaSnapshots ?? []).allSatisfy { areaIDs.contains($0.id) && $0.isValid }
      && Set((areaSnapshots ?? []).map(\.id)).count == (areaSnapshots ?? []).count
      && (areaSnapshots?.count ?? 0) <= 100
      && (1...1_000_000).contains(revision) && createdAt.timeIntervalSince1970.isFinite
      && revisions.count <= 100 && Set(revisions.map(\.revision)).count == revisions.count
      && revisions.allSatisfy {
        $0.id == id && $0.revision < revision && $0.isValid(snapshots: snapshots, sources: sources)
      }
      && ((try? JSONEncoder().encode(origin).count).map { $0 <= 128 * 1024 } ?? false)
  }
}
public struct ApprovedAssetAssociation: Codable, Equatable, Sendable, Identifiable {
  public var id: UUID
  public var assetSnapshotID: UUID
  public var citationID: UUID
  public var reason: String
  public var approvedAt: Date
}
/// Optional case extension. Historical area/source identities intentionally survive removal of live rows.
public struct MissionInvestigation: Codable, Equatable, Sendable {
  public var version: Int
  public var status: String
  public var completionCriteria: String
  public var conclusion: String
  public var unresolvedQuestions: String
  public var closureRationale: String
  public var snapshots: [InvestigationSnapshot]
  public var citations: [InvestigationCitation]
  public var findings: [Finding]
  public var associations: [ApprovedAssetAssociation]
  public func isValid(tasks: [MissionTask], inbox: [MissionMonitoringEvent]) -> Bool {
    let assetSnapshots = Set(snapshots.filter { $0.asset != nil }.map(\.id))
    let assets = Set(snapshots.compactMap { $0.asset?.id })
    let sources = Set(citations.map(\.id))
    let findingIDs = Set(findings.map(\.id))
    let outstanding =
      tasks.contains { !$0.done }
      || findings.contains { ["Unreviewed", "Uncertain"].contains($0.state) }
      || !unresolvedQuestions.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty
      || inbox.contains { !["Closed", "Dismissed"].contains($0.reviewState) }
    return version == 1 && ["Open", "In review", "Closed"].contains(status)
      && [completionCriteria, conclusion, unresolvedQuestions, closureRationale].allSatisfy {
        $0.count <= 32000
      }
      && snapshots.count <= 1000 && Set(snapshots.map(\.id)).count == snapshots.count
      && snapshots.allSatisfy(\.isValid)
      && snapshots.compactMap(\.relationship).allSatisfy {
        assets.contains($0.fromAssetID) && assets.contains($0.toAssetID)
      }
      && citations.count <= 1000 && sources.count == citations.count
      && citations.allSatisfy(\.isValid)
      && findings.count <= 200 && findingIDs.count == findings.count
      && findings.allSatisfy { $0.isValid(snapshots: assetSnapshots, sources: sources) }
      && associations.count <= 1000 && Set(associations.map(\.id)).count == associations.count
      && associations.allSatisfy {
        assetSnapshots.contains($0.assetSnapshotID) && sources.contains($0.citationID)
          && !$0.reason.isEmpty && $0.reason.count <= 4000
          && $0.approvedAt.timeIntervalSince1970.isFinite
      }
      && tasks.allSatisfy { $0.findingID.map { findingIDs.contains($0) } ?? true }
      && (status != "Closed" || !outstanding
        || !closureRationale.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty)
  }
}
public struct AssetRegistryDocument: Codable, Equatable, Sendable {
  public var version: Int
  public var assets: [Asset]
  public var relationships: [AssetRelationship]
  public var isValid: Bool {
    let ids = Set(assets.map(\.id))
    return version == 1 && assets.count <= 2000 && ids.count == assets.count
      && assets.allSatisfy(\.isValid)
      && relationships.count <= 5000 && Set(relationships.map(\.id)).count == relationships.count
      && relationships.allSatisfy {
        $0.isValid && ids.contains($0.fromAssetID) && ids.contains($0.toAssetID)
      }
  }
}
public enum InvestigationDocuments {
  /// expectedData is the exact data loaded before editing (nil means the file must not exist).
  /// A shared .lock path, last-good backup and atomic write preserve concurrent/corrupt documents.
  public static func saveRegistry(
    _ registry: AssetRegistryDocument, to url: URL, expectedData: Data?
  ) throws {
    guard registry.isValid else { throw FeedError.malformed }
    let encoder = JSONEncoder()
    encoder.outputFormatting = [.prettyPrinted, .sortedKeys]
    let data = try encoder.encode(registry)
    guard data.count <= 8 * 1024 * 1024 else { throw FeedError.oversized }
    let fm = FileManager.default
    try fm.createDirectory(at: url.deletingLastPathComponent(), withIntermediateDirectories: true)
    let lock = url.path + ".lock"
    let descriptor = Darwin.open(url.path + ".lock", O_WRONLY | O_CREAT | O_EXCL, 0o600)
    guard descriptor >= 0 else { throw FeedError.malformed }
    defer {
      Darwin.close(descriptor)
      Darwin.unlink(lock)
    }
    let lockData = Data("\(getpid())\nHeliosMac\n\(ProcessInfo.processInfo.hostName)\n".utf8)
    lockData.withUnsafeBytes { _ = Darwin.write(descriptor, $0.baseAddress, $0.count) }
    if fm.fileExists(atPath: url.path) {
      let attributes = try url.resourceValues(forKeys: [.isSymbolicLinkKey, .fileSizeKey])
      guard attributes.isSymbolicLink != true, (attributes.fileSize ?? Int.max) <= 8 * 1024 * 1024
      else { throw FeedError.malformed }
      let previous = try Data(contentsOf: url)
      let loaded = try loadRegistry(from: url)
      guard expectedData == previous, loaded.isValid,
        Set(loaded.assets.map(\.id)).isSubset(of: Set(registry.assets.map(\.id))),
        Set(loaded.relationships.map(\.id)).isSubset(of: Set(registry.relationships.map(\.id)))
      else { throw FeedError.malformed }
      let backup = URL(fileURLWithPath: url.path + ".backup")
      if fm.fileExists(atPath: backup.path),
        try backup.resourceValues(forKeys: [.isSymbolicLinkKey]).isSymbolicLink == true
      {
        throw FeedError.malformed
      }
      try previous.write(to: backup, options: .atomic)
    } else if expectedData != nil {
      throw FeedError.malformed
    }
    try data.write(to: url, options: .atomic)
  }
  public static func loadRegistry(from url: URL) throws -> AssetRegistryDocument {
    guard FileManager.default.fileExists(atPath: url.path) else {
      return AssetRegistryDocument(version: 1, assets: [], relationships: [])
    }
    let result = try SarProcessor.read(
      AssetRegistryDocument.self, from: url, maximumBytes: 8 * 1024 * 1024)
    guard result.isValid else { throw FeedError.malformed }
    return result
  }
}
public func validInvestigationTaskDependencies(_ tasks: [MissionTask]) -> Bool {
  let rows = Dictionary(tasks.map { ($0.id, $0) }, uniquingKeysWith: { a, _ in a })
  guard rows.count == tasks.count else { return false }
  var colors: [UUID: Int] = [:]
  func visit(_ id: UUID) -> Bool {
    if colors[id] == 1 { return false }
    if colors[id] == 2 { return true }
    guard let task = rows[id] else { return false }
    colors[id] = 1
    let prerequisites = task.dependencyIDs ?? []
    guard prerequisites.count <= 200 && Set(prerequisites).count == prerequisites.count else {
      return false
    }
    for ref in prerequisites {
      guard visit(ref), !task.done || rows[ref]?.done == true else { return false }
    }
    colors[id] = 2
    return true
  }
  return tasks.allSatisfy { visit($0.id) }
}
