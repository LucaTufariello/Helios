import Foundation

public struct MissionStableControl: Codable, Equatable, Sendable {
  public var id: UUID
  public var name: String
  public var role: String
  public var region: SentinelRegion
  public var isValid: Bool {
    !name.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty && name.count <= 120
      && ["Fit", "Verify"].contains(role)
  }
}
public struct MissionMonitoring: Codable, Equatable, Sendable {
  public var version = 1
  public var enabled = false
  public var baselineEvidenceID: UUID?
  public var discoveryMonitorID: UUID?
  public var polarization = "VV"
  public var intervalHours = 24
  public var maximumShiftPixels = 2.0
  public var maximumResidualPixels = 0.5
  public var minimumCorrelation = 0.6
  public var thresholdDb = 3.0
  public var averagingWindow = 5
  public var minimumPixels = 25
  public var controls: [MissionStableControl] = []
  public init() {}
  public func isValid(
    pins: Set<UUID>, monitors: Set<UUID>? = nil,
    radarBands: [UUID: String]? = nil
  ) -> Bool {
    guard
      version == 1 && ["VV", "VH"].contains(polarization) && (1...168).contains(intervalHours)
        && maximumShiftPixels.isFinite && (0.5...8).contains(maximumShiftPixels)
        && maximumResidualPixels.isFinite && (0.1...1).contains(maximumResidualPixels)
        && minimumCorrelation.isFinite && (0.5...0.99).contains(minimumCorrelation)
        && thresholdDb.isFinite && (1...20).contains(thresholdDb)
        && [3, 5, 7].contains(averagingWindow) && (9...10_000).contains(minimumPixels)
        && controls.count <= 12 && Set(controls.map(\.id)).count == controls.count
        && controls.allSatisfy(\.isValid)
        && (discoveryMonitorID.map { monitors?.contains($0) ?? true } ?? true)
        && (baselineEvidenceID.map { pins.contains($0) } ?? !enabled)
    else { return false }
    if let baselineEvidenceID, let radarBands, radarBands[baselineEvidenceID] != polarization {
      return false
    }
    if enabled
      && (controls.filter { $0.role == "Fit" }.count < 3
        || controls.filter { $0.role == "Verify" }.count < 3)
    {
      return false
    }
    for i in controls.indices {
      for j in controls.indices where j < i {
        let a = controls[i].region
        let b = controls[j].region
        if a.west < b.east && a.east > b.west && a.south < b.north && a.north > b.south {
          return false
        }
      }
    }
    return true
  }
}
public struct MissionMonitoringEvent: Codable, Equatable, Sendable, Identifiable {
  public var version: Int
  public var id: UUID
  public var key: String
  public var signature: String?
  public var areaID: UUID?
  public var baselineEvidenceID: UUID?
  public var areaName: String
  public var title: String
  public var sourceID: String
  public var kind: String
  public var status: String
  public var reviewState: String
  public var rationale: String
  public var reason: String
  public var createdAt: Date
  public var updatedAt: Date
  public var sourceTime: Date?
  public var snoozedUntil: Date?
  public var jobID: UUID?
  public var previousJobIDs: [UUID]?
  public var candidateCount: Int?
  public var maximumResidualPixels: Double?
  public var commonValidFraction: Double?
  public func isValid(areas: Set<UUID>, evidence: Set<UUID>) -> Bool {
    func sha(_ value: String) -> Bool {
      value.count == 64
        && value.utf8.allSatisfy { (48...57).contains($0) || (97...102).contains($0) }
    }
    return version == 1 && sha(key) && (kind != "Analysis" || (signature.map(sha) ?? false))
      && (areaID.map { areas.contains($0) } ?? true)
      && (baselineEvidenceID.map { evidence.contains($0) } ?? true)
      && !title.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty && title.count <= 1024
      && areaName.count <= 160 && sourceID.count <= 512 && reason.count <= 2000
      && rationale.count <= 4000
      && ["Analysis", "Acquisition"].contains(kind)
      && ["Queued", "Running", "Ready", "Blocked", "Failed", "Interrupted", "Stale", "Discovered"]
        .contains(status)
      && ["New", "Investigating", "Closed", "Dismissed", "Snoozed"].contains(reviewState)
      && createdAt.timeIntervalSince1970.isFinite && updatedAt.timeIntervalSince1970.isFinite
      && (sourceTime?.timeIntervalSince1970.isFinite ?? true)
      && (snoozedUntil?.timeIntervalSince1970.isFinite ?? true)
      && (previousJobIDs?.count ?? 0) <= 20
      && (candidateCount.map { (0...500).contains($0) } ?? true)
      && (maximumResidualPixels.map { $0.isFinite && (0...100).contains($0) } ?? true)
      && (commonValidFraction.map { $0.isFinite && (0...1).contains($0) } ?? true)
  }
}
/// Preserve scientific report extensions without discarding nested provenance.
public indirect enum PortableJSONValue: Codable, Equatable, Sendable {
  case null
  case bool(Bool)
  case number(Double)
  case string(String)
  case array([PortableJSONValue])
  case object([String: PortableJSONValue])
  public init(from decoder: any Decoder) throws {
    let c = try decoder.singleValueContainer()
    if c.decodeNil() {
      self = .null
    } else if let v = try? c.decode(Bool.self) {
      self = .bool(v)
    } else if let v = try? c.decode(Double.self) {
      self = .number(v)
    } else if let v = try? c.decode(String.self) {
      self = .string(v)
    } else if let v = try? c.decode([PortableJSONValue].self) {
      self = .array(v)
    } else {
      self = .object(try c.decode([String: PortableJSONValue].self))
    }
  }
  public func encode(to encoder: any Encoder) throws {
    var c = encoder.singleValueContainer()
    switch self {
    case .null: try c.encodeNil()
    case .bool(let v): try c.encode(v)
    case .number(let v): try c.encode(v)
    case .string(let v): try c.encode(v)
    case .array(let v): try c.encode(v)
    case .object(let v): try c.encode(v)
    }
  }
}
