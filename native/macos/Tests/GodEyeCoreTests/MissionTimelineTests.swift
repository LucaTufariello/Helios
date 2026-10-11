import Foundation
import XCTest

@testable import GodEyeCore

final class MissionTimelineTests: XCTestCase {
  private let now = Date(timeIntervalSince1970: 1_800_000_000)
  private func area() throws -> MissionArea {
    MissionArea(
      name: "Port", purpose: "Review",
      region: try SentinelRegion(west: 8, south: 44, east: 9, north: 45))
  }
  private func entry(_ sourceID: String = "scene-a", kind: MissionTimelineKind = .optical)
    -> MissionTimelineEntry
  {
    MissionTimelineEntry(
      id: UUID(), kind: kind, title: "Acquisition", time: now.addingTimeInterval(-100),
      source: "Sentinel-2", sourceID: sourceID, bounds: [8, 44, 9, 45], rasterID: UUID().uuidString,
      displayBand: "RGB", resolutionMetres: 10)
  }
  func testAcquisitionSnapshotRoundTripAndLegacyCompatibility() throws {
    var item = MissionItem(kind: .optical, referenceID: UUID().uuidString, title: "Image")
    let legacy = try JSONEncoder().encode(item)
    XCTAssertNil(try JSONDecoder().decode(MissionItem.self, from: legacy).acquisition)
    item.acquisition = MissionAcquisition(
      sourceID: "scene", sensor: "Sentinel-2", acquiredAt: now, bounds: [8, 44, 9, 45],
      displayBand: "RGB", resolutionMetres: 10)
    XCTAssertTrue(item.isValid)
    XCTAssertEqual(
      try JSONDecoder().decode(MissionItem.self, from: JSONEncoder().encode(item)), item)
    item.acquisition?.bounds = [9, 44, 8, 45]
    XCTAssertFalse(item.isValid)
    item.acquisition?.bounds = nil
    item.acquisition?.resolutionMetres = .nan
    XCTAssertFalse(item.isValid)
    item.acquisition?.resolutionMetres = nil
    item.acquisition?.acquiredAt = Date(timeIntervalSince1970: .infinity)
    XCTAssertFalse(item.isValid)
  }
  func testDateFilterUsesExclusiveEndAndExplicitUnknownPolicy() {
    var filter = MissionTimelineFilter()
    filter.start = now.addingTimeInterval(-100)
    filter.endExclusive = now
    var row = entry()
    XCTAssertTrue(filter.accepts(row))
    row.time = now
    XCTAssertFalse(filter.accepts(row))
    row.time = now.addingTimeInterval(-101)
    XCTAssertFalse(filter.accepts(row))
    row.time = nil
    XCTAssertTrue(filter.accepts(row))
    filter.includeUndated = false
    XCTAssertFalse(filter.accepts(row))
  }
  func testSortingDoesNotInventObservationDates() {
    let old = entry("old")
    var recent = entry("recent")
    recent.time = now
    var unknown = entry("unknown")
    unknown.time = nil
    XCTAssertEqual(
      MissionTimeline.sorted([unknown, old, recent], filter: MissionTimelineFilter()).map(
        \.sourceID), ["recent", "old", "unknown"])
  }
  func testAreaFilterUsesBoundsPointsOrExplicitAssociation() throws {
    let area = try area()
    var row = entry()
    XCTAssertTrue(row.relates(to: area))
    row.bounds = [12, 44, 13, 45]
    XCTAssertFalse(row.relates(to: area))
    row.coordinate = Coordinate(latitude: 44.5, longitude: 8.5)
    XCTAssertTrue(row.relates(to: area))
    row.coordinate = nil
    row.areaIDs = [area.id]
    XCTAssertTrue(row.relates(to: area))
    var filter = MissionTimelineFilter()
    filter.area = area
    filter.kind = .radar
    XCTAssertFalse(filter.accepts(row))
    filter.kind = .optical
    filter.query = "sentinel"
    XCTAssertTrue(filter.accepts(row))
    filter.query = "unrelated"
    XCTAssertFalse(filter.accepts(row))
  }
  func testAssessmentDeduplicatesAcquisitionsAndExcludesUnusableDates() throws {
    let first = entry()
    let duplicate = entry()
    var unavailable = entry("missing")
    unavailable.available = false
    var old = entry("old")
    old.time = now.addingTimeInterval(-10000)
    var future = entry("future")
    future.time = now.addingTimeInterval(1)
    var undated = entry("undated")
    undated.time = nil
    let note = entry("note", kind: .note)
    let result = MissionTimeline.assess(
      [first, duplicate, unavailable, old, future, undated, note], area: try area(),
      since: now.addingTimeInterval(-1000), now: now)
    XCTAssertEqual(result.opticalCount, 1)
    XCTAssertEqual(result.radarCount, 0)
    XCTAssertEqual(result.unavailableCount, 1)
    XCTAssertEqual(result.undatedCount, 1)
    XCTAssertEqual(result.newest, first.time)
    XCTAssertTrue(result.gaps.contains { $0.contains("Fewer than two") })
    let complete = MissionTimeline.assess(
      [first, entry("second"), entry("radar", kind: .radar)], area: try area(),
      since: now.addingTimeInterval(-1000), now: now)
    XCTAssertTrue(complete.gaps.isEmpty)
  }
  func testComparisonRejectsDuplicateMissingReversedAndNonoverlappingInputs() {
    let a = entry()
    var b = entry("scene-b")
    b.time = now
    XCTAssertNil(MissionTimeline.comparisonProblem(a, b))
    XCTAssertNotNil(MissionTimeline.comparisonProblem(nil, b))
    XCTAssertNotNil(MissionTimeline.comparisonProblem(b, a))
    b.sourceID = a.sourceID
    XCTAssertNotNil(MissionTimeline.comparisonProblem(a, b))
    b.sourceID = "scene-b"
    b.available = false
    XCTAssertNotNil(MissionTimeline.comparisonProblem(a, b))
    b.available = true
    b.bounds = [20, 30, 21, 31]
    XCTAssertNotNil(MissionTimeline.comparisonProblem(a, b))
    b.bounds = a.bounds
    b.time = nil
    XCTAssertNotNil(MissionTimeline.comparisonProblem(a, b))
  }
  func testComparisonAlwaysExplainsVisualLimitsAndSensorDifferences() {
    let a = entry()
    var b = entry("radar", kind: .radar)
    b.source = "Sentinel-1"
    b.displayBand = "VV"
    b.resolutionMetres = 20
    XCTAssertEqual(MissionTimeline.comparisonWarnings(a, b).count, 4)
    XCTAssertEqual(MissionTimeline.comparisonWarnings(a, a).count, 1)
  }
  func testCSVPreservesProvenanceAndEscapesSpreadsheetText() {
    var row = entry()
    row.title = " =HYPERLINK(\"test\")"
    row.detail = "Line one\nLine two, quoted \"text\""
    row.kind = .note
    let csv = MissionTimeline.csv([row])
    XCTAssertTrue(csv.contains("\"' =HYPERLINK(\"\"test\"\")\""))
    XCTAssertTrue(csv.contains("\"Line one\nLine two, quoted \"\"text\"\"\""))
    XCTAssertTrue(csv.contains("\"Note authored\""))
    XCTAssertTrue(csv.contains("\"scene-a\""))
    XCTAssertTrue(csv.contains("Z\""))
    XCTAssertTrue(csv.hasSuffix("\r\n"))
  }
}
