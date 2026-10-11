import AppKit
import GodEyeCore
import SwiftUI
import UniformTypeIdentifiers

struct MissionTimelineView: View {
  @ObservedObject var model: WorkspaceModel
  @ObservedObject var missions: MissionWorkspace
  @ObservedObject var optical: OpticalWorkspace
  @ObservedObject var sar: SarWorkspace
  @State private var entries: [MissionTimelineEntry] = []
  @State private var areaID: UUID?
  @State private var kind: MissionTimelineKind?
  @State private var query = ""
  @State private var useDates = false
  @State private var start = Date().addingTimeInterval(-90 * 86400)
  @State private var end = Date()
  @State private var includeUndated = true
  @State private var lookbackDays = 30
  @State private var before: UUID?
  @State private var after: UUID?
  @State private var refreshed = Date()
  @State private var message: String?
  private var area: MissionArea? { missions.brief.areas.first { $0.id == areaID } }
  private var calendar: Calendar {
    var c = Calendar(identifier: .gregorian)
    c.timeZone = TimeZone(secondsFromGMT: 0)!
    return c
  }
  private var validDates: Bool {
    !useDates || calendar.startOfDay(for: start) <= calendar.startOfDay(for: end)
  }
  private var visible: [MissionTimelineEntry] {
    guard validDates else { return [] }
    var filter = MissionTimelineFilter()
    filter.area = area
    filter.kind = kind
    filter.query = query
    filter.includeUndated = includeUndated
    if useDates {
      filter.start = calendar.startOfDay(for: start)
      filter.endExclusive = calendar.date(
        byAdding: .day, value: 1, to: calendar.startOfDay(for: end))
    }
    return MissionTimeline.sorted(entries, filter: filter)
  }
  private var a: MissionTimelineEntry? { entries.first { $0.id == before } }
  private var b: MissionTimelineEntry? { entries.first { $0.id == after } }
  var body: some View {
    VStack(alignment: .leading, spacing: 18) {
      HStack {
        Text("Follow the evidence through time").font(.headline)
        Spacer()
        Button("Refresh") { refresh() }
        Button("Save acquisition metadata") {
          model.captureMissionAcquisitions()
          refresh()
        }
        .help(
          "Preserve acquisition times and footprints for older mission links, without copying imagery"
        )
        Button("Export filtered CSV…") { export() }.disabled(visible.isEmpty)
      }
      Text(
        "Acquisition and observation times are distinct from note authoring times. All times below are UTC. Unknown dates remain explicit."
      )
      .font(.caption).foregroundStyle(.secondary)
      GroupBox("Filter evidence") {
        VStack(alignment: .leading, spacing: 10) {
          HStack {
            Picker("Area", selection: $areaID) {
              Text("Entire mission").tag(UUID?.none)
              ForEach(missions.brief.areas) { Text($0.name).tag(Optional($0.id)) }
            }
            Picker("Type", selection: $kind) {
              Text("All types").tag(MissionTimelineKind?.none)
              ForEach(MissionTimelineKind.allCases, id: \.self) {
                Text($0.rawValue).tag(Optional($0))
              }
            }
          }
          HStack {
            TextField("Search title, source or review text", text: $query).textFieldStyle(
              .roundedBorder)
            Toggle("Include unknown dates", isOn: $includeUndated)
          }
          HStack {
            Toggle("Date range", isOn: $useDates)
            if useDates {
              DatePicker("From (UTC)", selection: $start, displayedComponents: .date)
              DatePicker("Through (UTC)", selection: $end, displayedComponents: .date)
            }
            Spacer()
            Text("\(visible.count) / \(entries.count) entries").font(.caption).foregroundStyle(
              .secondary)
          }.environment(\.timeZone, TimeZone(secondsFromGMT: 0)!)
          if !validDates {
            Text("The start date must precede or equal the end date.").foregroundStyle(.orange)
          }
        }.padding(10)
      }
      if let area {
        assessment(area)
      } else {
        Text("Choose an area to inspect recent acquisition coverage and evidence gaps.").font(
          .caption
        ).foregroundStyle(.secondary)
      }
      comparison
      if let message { Text(message).font(.caption).foregroundStyle(.secondary) }
      if visible.isEmpty {
        ContentUnavailableView(
          "No matching evidence", systemImage: "calendar.badge.exclamationmark",
          description: Text(
            "Link imagery or source records in Evidence, add observations/notes, or broaden the filters."
          ))
      } else {
        LazyVStack(alignment: .leading, spacing: 10) {
          ForEach(visible) { entry in row(entry) }
        }
      }
      Text(
        "Availability last checked \(utc(refreshed)). Area matches use preview bounds, source coordinates or explicit links; they do not prove cloud-free or valid-pixel coverage."
      )
      .font(.caption).foregroundStyle(.secondary)
    }
    .onAppear {
      refresh()
      restorePair()
    }
    .onChange(of: missions.selectedID) { _, _ in
      areaID = nil
      before = nil
      after = nil
      refresh()
      restorePair()
    }
    .onChange(of: missions.plan?.updatedAt) { _, _ in refresh() }
    .onChange(of: optical.images.map(\.id)) { _, _ in refresh() }
    .onChange(of: sar.jobs.map { $0.id + $0.result.state.rawValue }) { _, _ in refresh() }
  }
  private func refresh() {
    entries = model.missionTimelineEntries()
    refreshed = Date()
    if !missions.brief.areas.contains(where: { $0.id == areaID }) { areaID = nil }
    if !entries.contains(where: { $0.id == before }) { before = nil }
    if !entries.contains(where: { $0.id == after }) { after = nil }
  }
  private func restorePair() {
    before = entries.first { $0.rasterID == missions.baseID }?.id
    after = entries.first { $0.rasterID == missions.topID }?.id
  }
  private func assessment(_ area: MissionArea) -> some View {
    let summary = MissionTimeline.assess(
      entries, area: area,
      since: refreshed.addingTimeInterval(-Double(lookbackDays) * 86400), now: refreshed)
    return GroupBox("Area evidence · \(area.name)") {
      VStack(alignment: .leading, spacing: 10) {
        HStack {
          Text(
            "\(summary.opticalCount) optical · \(summary.radarCount) radar distinct recent acquisitions"
          ).font(.headline)
          Spacer()
          Button("Show area") { model.showMissionArea(area) }
        }
        Stepper("Look back \(lookbackDays) days", value: $lookbackDays, in: 1...365)
        Text(
          "Uses all mission-linked imagery for this area, independently of the timeline filters."
        ).font(.caption).foregroundStyle(.secondary)
        if summary.gaps.isEmpty {
          Label(
            "Recent optical/radar evidence is available. Independent spatial and radiometric validation is still required.",
            systemImage: "info.circle"
          ).font(.caption)
        } else {
          ForEach(summary.gaps, id: \.self) { gap in
            Label(gap, systemImage: "exclamationmark.circle").font(.caption).foregroundStyle(
              .orange)
          }
        }
        HStack {
          Button("Find more imagery") {
            model.showMissionArea(area)
            model.satellite.page = "Reference imagery"
            model.openPanel(.satellite)
          }
          Button("Create a review task") {
            let title = "Review evidence gaps: \(area.name)"
            if missions.brief.tasks.contains(where: { $0.title == title && !$0.done }) {
              message = "An open evidence-gap review task already exists."
            } else {
              var task = MissionTask(title: title)
              task.areaID = area.id
              if missions.addTask(task) { message = "Evidence-gap review task added." }
            }
          }
        }
      }.padding(10)
    }
  }
  private var comparison: some View {
    GroupBox("Before / after on the map") {
      VStack(alignment: .leading, spacing: 10) {
        Text("Use A and B on acquisition rows. The pair stays selected while you change filters.")
          .font(.caption).foregroundStyle(.secondary)
        Text("A · \(a.map { utc($0.time) + " · " + $0.title } ?? "Choose baseline")").lineLimit(2)
        Text("B · \(b.map { utc($0.time) + " · " + $0.title } ?? "Choose later acquisition")")
          .lineLimit(2)
        if let issue = MissionTimeline.comparisonProblem(a, b) {
          Text(issue).font(.caption).foregroundStyle(.secondary)
        } else if let a, let b {
          ForEach(MissionTimeline.comparisonWarnings(a, b), id: \.self) {
            Text($0).font(.caption).foregroundStyle(.secondary)
          }
        }
        HStack {
          Button("Compare on map") {
            _ = model.compareMissionTimeline(before: before, after: after)
          }
          .buttonStyle(.borderedProminent).disabled(MissionTimeline.comparisonProblem(a, b) != nil)
          Button("Clear pair") {
            before = nil
            after = nil
          }.disabled(before == nil && after == nil)
          if missions.comparisonEnabled {
            Slider(value: $missions.blend, in: 0...1)
            Text("B \(Int(missions.blend * 100))%")
            Button("Hide comparison") { missions.comparisonEnabled = false }
          }
        }
        Text(
          "The toolbar's Save changes retains the active map comparison. Visual blending does not calculate physical change."
        )
        .font(.caption).foregroundStyle(.secondary)
      }.padding(10)
    }
  }
  private func row(_ entry: MissionTimelineEntry) -> some View {
    HStack(alignment: .top, spacing: 16) {
      VStack(alignment: .leading, spacing: 4) {
        Text(utc(entry.time)).font(.caption.monospacedDigit())
        Text(entry.kind == .note ? "Note authored" : "Acquired / observed").font(.caption2)
          .foregroundStyle(.secondary)
      }.frame(width: 145, alignment: .leading)
      VStack(alignment: .leading, spacing: 6) {
        Text(entry.title).font(.headline).textSelection(.enabled)
        Text("\(entry.kind.rawValue) · \(entry.source)").font(.caption).foregroundStyle(.secondary)
        if entry.kind.isRaster {
          HStack {
            Text(entry.available ? "Available locally" : "Local raster unavailable")
              .foregroundStyle(entry.available ? Color.secondary : .orange)
            if let band = entry.displayBand { Text("· \(band)") }
            if let spacing = entry.resolutionMetres {
              Text(
                "· \(spacing, specifier: "%.1f") m \(entry.kind == .radar ? "output spacing" : "source spacing")"
              )
            }
          }.font(.caption)
        }
        Text(entry.detail).font(.caption).foregroundStyle(.secondary).lineLimit(3)
        HStack {
          Button("Show on map") { model.showTimelineEntry(entry) }
            .disabled(
              entry.kind.isRaster && !entry.available
                || (entry.evidenceID == nil && entry.coordinate == nil && entry.areaIDs.isEmpty))
          if entry.kind.isRaster {
            Button(before == entry.id ? "A selected" : "Set A") { before = entry.id }
            Button(after == entry.id ? "B selected" : "Set B") { after = entry.id }
          }
          if entry.kind == .object {
            Button("Review observations") { missions.section = "Objects" }
          }
          if entry.kind == .note { Button("Review notes") { missions.section = "Notes" } }
        }
      }
      Spacer(minLength: 0)
    }.padding(12).frame(maxWidth: .infinity, alignment: .leading)
      .background(Color.gray.opacity(0.06), in: RoundedRectangle(cornerRadius: 8))
  }
  private func utc(_ date: Date?) -> String {
    guard let date else { return "Unknown time" }
    let f = DateFormatter()
    f.locale = Locale(identifier: "en_US_POSIX")
    f.timeZone = TimeZone(secondsFromGMT: 0)
    f.dateFormat = "yyyy-MM-dd HH:mm:ss 'UTC'"
    return f.string(from: date)
  }
  private func export() {
    let panel = NSSavePanel()
    panel.allowedContentTypes = [.commaSeparatedText]
    panel.nameFieldStringValue = "mission-timeline.csv"
    guard panel.runModal() == .OK, let url = panel.url else { return }
    do {
      try Data(MissionTimeline.csv(visible).utf8).write(to: url, options: .atomic)
      message = "Filtered timeline exported with UTC timestamps."
    } catch { missions.error = "The timeline could not be exported." }
  }
}
