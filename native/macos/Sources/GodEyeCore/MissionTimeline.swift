import Foundation

/// A bounded acquisition snapshot survives removal of the linked local raster.
public struct MissionAcquisition: Codable, Equatable, Sendable {
  public var sourceID: String
  public var sensor: String
  public var acquiredAt: Date?
  public var bounds: [Double]?
  public var displayBand: String?
  public var resolutionMetres: Double?
  public init(
    sourceID: String, sensor: String, acquiredAt: Date?, bounds: [Double]?,
    displayBand: String? = nil, resolutionMetres: Double? = nil
  ) {
    self.sourceID = sourceID
    self.sensor = sensor
    self.acquiredAt = acquiredAt
    self.bounds = bounds
    self.displayBand = displayBand
    self.resolutionMetres = resolutionMetres
  }
  public var isValid: Bool {
    !sourceID.isEmpty && sourceID.count <= 512 && !sensor.isEmpty && sensor.count <= 120
      && (acquiredAt?.timeIntervalSince1970.isFinite ?? true)
      && (bounds.map(ChangeReviewResult.validBounds) ?? true)
      && (displayBand.map { !$0.isEmpty && $0.count <= 80 } ?? true)
      && (resolutionMetres.map { $0.isFinite && $0 > 0 } ?? true)
  }
}
public enum MissionTimelineKind: String, CaseIterable, Codable, Sendable {
  case optical = "Optical acquisition"
  case radar = "Radar acquisition"
  case record = "Source observation"
  case object = "Object observation"
  case note = "Review note"
  public var isRaster: Bool { self == .optical || self == .radar }
}
public struct MissionTimelineEntry: Identifiable, Codable, Equatable, Sendable {
  public var id: UUID
  public var kind: MissionTimelineKind
  public var title: String
  /// Acquisition/observation time; note entries use their explicitly labeled authoring time.
  public var time: Date?
  public var source: String
  public var sourceID: String?
  public var bounds: [Double]?
  public var coordinate: Coordinate?
  public var areaIDs: [UUID]
  public var evidenceID: UUID?
  public var available: Bool
  public var rasterID: String?
  public var displayBand: String?
  public var resolutionMetres: Double?
  public var detail: String
  public init(
    id: UUID, kind: MissionTimelineKind, title: String, time: Date?, source: String,
    sourceID: String? = nil, bounds: [Double]? = nil, coordinate: Coordinate? = nil,
    areaIDs: [UUID] = [], evidenceID: UUID? = nil, available: Bool = true, rasterID: String? = nil,
    displayBand: String? = nil, resolutionMetres: Double? = nil, detail: String = ""
  ) {
    self.id = id
    self.kind = kind
    self.title = title
    self.time = time
    self.source = source
    self.sourceID = sourceID
    self.bounds = bounds
    self.coordinate = coordinate
    self.areaIDs = areaIDs
    self.evidenceID = evidenceID
    self.available = available
    self.rasterID = rasterID
    self.displayBand = displayBand
    self.resolutionMetres = resolutionMetres
    self.detail = detail
  }
  public func relates(to area: MissionArea) -> Bool {
    if areaIDs.contains(area.id) { return true }
    let r = area.region
    if let bounds, RasterComparison.overlaps(bounds, [r.west, r.south, r.east, r.north]) {
      return true
    }
    if let coordinate {
      return coordinate.latitude >= r.south && coordinate.latitude <= r.north
        && coordinate.longitude >= r.west && coordinate.longitude <= r.east
    }
    return false
  }
}
public struct MissionTimelineFilter: Sendable {
  public var area: MissionArea?
  public var kind: MissionTimelineKind?
  public var start: Date?
  public var endExclusive: Date?
  public var includeUndated = true
  public var query = ""
  public init() {}
  public func accepts(_ entry: MissionTimelineEntry) -> Bool {
    if let area, !entry.relates(to: area) { return false }
    if let kind, kind != entry.kind { return false }
    if !query.isEmpty
      && !(entry.title + " " + entry.source + " " + entry.detail).localizedCaseInsensitiveContains(
        query)
    {
      return false
    }
    guard let time = entry.time else { return includeUndated }
    if let start, time < start { return false }
    if let endExclusive, time >= endExclusive { return false }
    return true
  }
}
public struct MissionEvidenceAssessment: Equatable, Sendable {
  public var opticalCount: Int
  public var radarCount: Int
  public var unavailableCount: Int
  public var undatedCount: Int
  public var newest: Date?
  public var gaps: [String]
}
public enum MissionTimeline {
  public static func sorted(_ entries: [MissionTimelineEntry], filter: MissionTimelineFilter)
    -> [MissionTimelineEntry]
  {
    entries.filter(filter.accepts).sorted {
      if $0.time != $1.time { return ($0.time ?? .distantPast) > ($1.time ?? .distantPast) }
      return $0.id.uuidString < $1.id.uuidString
    }
  }
  /// Bounds intersection is a discovery aid, never valid-pixel coverage or scientific readiness.
  public static func assess(
    _ entries: [MissionTimelineEntry], area: MissionArea, since: Date, now: Date
  ) -> MissionEvidenceAssessment {
    let rasters = entries.filter { $0.kind.isRaster && $0.relates(to: area) }
    let recent = rasters.filter { $0.available && $0.time.map { $0 >= since && $0 <= now } == true }
    func count(_ kind: MissionTimelineKind) -> Int {
      Set(recent.filter { $0.kind == kind }.map { $0.sourceID ?? $0.id.uuidString }).count
    }
    let optical = count(.optical)
    let radar = count(.radar)
    let missing = rasters.filter { !$0.available }.count
    let undated = rasters.filter { $0.time == nil }.count
    var gaps: [String] = []
    if optical == 0 {
      gaps.append("No available dated optical acquisition in the lookback window.")
    }
    if radar == 0 { gaps.append("No available dated radar acquisition in the lookback window.") }
    if missing > 0 { gaps.append("\(missing) linked raster(s) unavailable locally.") }
    if undated > 0 { gaps.append("\(undated) acquisition(s) have unknown observation time.") }
    if max(optical, radar) < 2 {
      gaps.append(
        "Fewer than two distinct recent acquisitions of either modality; a before/after pair may be missing."
      )
    }
    return MissionEvidenceAssessment(
      opticalCount: optical, radarCount: radar, unavailableCount: missing,
      undatedCount: undated, newest: recent.compactMap(\.time).max(), gaps: gaps)
  }
  public static func comparisonProblem(_ a: MissionTimelineEntry?, _ b: MissionTimelineEntry?)
    -> String?
  {
    guard let a, let b else { return "Choose a baseline and a later acquisition." }
    guard a.kind.isRaster, b.kind.isRaster, a.available, b.available, a.rasterID != nil,
      b.rasterID != nil
    else {
      return "Both acquisitions need an available local raster preview."
    }
    guard a.id != b.id, a.rasterID != b.rasterID,
      !(a.sourceID != nil && a.sourceID == b.sourceID && a.source == b.source)
    else {
      return
        "Choose two different acquisitions; duplicate crops or processing runs are not a new observation."
    }
    guard let first = a.time, let second = b.time, first < second else {
      return "The baseline must have a known acquisition time earlier than the comparison."
    }
    guard let ab = a.bounds, let bb = b.bounds, RasterComparison.overlaps(ab, bb) else {
      return "The acquisition bounds do not overlap, or their location is unknown."
    }
    return nil
  }
  public static func comparisonWarnings(_ a: MissionTimelineEntry, _ b: MissionTimelineEntry)
    -> [String]
  {
    var warnings = [
      "Visual comparison only. Bounds overlap does not establish image registration or usable pixel coverage."
    ]
    if a.source != b.source || a.kind != b.kind {
      warnings.append("Different sensors or modalities can produce apparent changes.")
    }
    if a.displayBand != b.displayBand { warnings.append("The selected display bands differ.") }
    if a.resolutionMetres != b.resolutionMetres {
      warnings.append("Source pixel spacing differs or is unknown.")
    }
    return warnings
  }
  /// Spreadsheet-safe CSV: user-authored text cannot become a formula on opening the export.
  public static func csv(_ entries: [MissionTimelineEntry]) -> String {
    let dates = ISO8601DateFormatter()
    func cell(_ value: String) -> String {
      let trimmed = value.trimmingCharacters(in: .whitespacesAndNewlines)
      let safe = ["=", "+", "-", "@"].contains(String(trimmed.prefix(1))) ? "'" + value : value
      return "\"" + safe.replacingOccurrences(of: "\"", with: "\"\"") + "\""
    }
    var rows = [
      "entry_id,kind,time_utc,time_basis,title,source,source_id,available,evidence_id,bounds_wsen,detail"
    ]
    for e in entries {
      rows.append(
        [
          e.id.uuidString, e.kind.rawValue, e.time.map(dates.string) ?? "",
          e.kind == .note ? "Note authored" : "Acquired/observed", e.title, e.source,
          e.sourceID ?? "",
          e.available ? "yes" : "no", e.evidenceID?.uuidString ?? "",
          e.bounds?.map { String($0) }.joined(separator: " ") ?? "", e.detail,
        ]
        .map(cell).joined(separator: ","))
    }
    return rows.joined(separator: "\r\n") + "\r\n"
  }
}
