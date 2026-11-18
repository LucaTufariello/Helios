import AppKit
import HeliosCore
import ImageIO
import SwiftUI

/// A dedicated work surface sharing the map's model, dataset libraries and selection.
struct MissionControlView: View {
  @ObservedObject var model: WorkspaceModel
  @ObservedObject var missions: MissionWorkspace
  @ObservedObject var optical: OpticalWorkspace
  @ObservedObject var sar: SarWorkspace
  @ObservedObject var satellite: SatelliteWorkspace
  @ObservedObject var automation: MissionAutomationRunner
  @ObservedObject var detection: MissionDetection
  @Binding var section: String
  @State private var query = ""
  @State private var evidenceID: UUID?
  @State private var areaEditingID: UUID?
  @State private var noteEditingID: UUID?
  @State private var areaName = ""
  @State private var areaPurpose = ""
  @State private var west = ""
  @State private var south = ""
  @State private var east = ""
  @State private var north = ""
  @State private var noteTitle = ""
  @State private var noteBody = ""
  @State private var noteReference = ""
  @State private var objectTitle = ""
  @State private var objectCategory = "Infrastructure"
  @State private var latitude = ""
  @State private var longitude = ""
  @State private var noteArea: UUID?
  @State private var noteEvidence: UUID?
  @State private var objectArea: UUID?
  @State private var objectEvidence: UUID?
  @State private var taskTitle = ""
  @State private var taskDue = Date().addingTimeInterval(86400)
  @State private var taskHasDue = true
  @State private var monitorKind = MissionMonitorKind.sentinel2
  @State private var monitorArea: UUID?
  @State private var monitorInterval = 24
  @State private var monitorCloud = 30.0
  @State private var formError: String?
  private let sections = [
    "Overview", "Areas", "Evidence", "Timeline", "Objects", "Notes", "Tasks", "Automations",
    "Activity",
  ]
  var body: some View {
    VStack(spacing: 0) {
      HStack(spacing: 14) {
        Image(systemName: "square.stack.3d.up.fill").font(.title2).foregroundStyle(.tint)
        VStack(alignment: .leading, spacing: 3) {
          Text("MISSION CONTROL").font(.system(size: 10, weight: .semibold)).tracking(1.5)
            .foregroundStyle(.secondary)
          Text(missions.plan?.name ?? "Mission workspace").font(.title2.weight(.semibold))
          Text(
            "\(missions.brief.areas.count) areas · \(missions.plan?.items.count ?? 0) evidence items · \(missions.brief.observations.count) observations"
          )
          .font(.caption).foregroundStyle(.secondary)
        }
        Spacer()
        Button("Show map", systemImage: "map") { model.revealMap?() }.buttonStyle(
          .borderedProminent)
        Button("Raw data", systemImage: "curlybraces") {
          model.rawScope = "Mission"
          model.openPanel(.raw)
          model.revealMap?()
        }.disabled(missions.plan == nil)
        Button("Export…", systemImage: "square.and.arrow.up") { missions.export() }.disabled(
          missions.plan == nil)
        Button(missions.dirty ? "Save changes" : "Saved", systemImage: "checkmark.circle") {
          missions.save()
        }
        .disabled(missions.plan == nil || !missions.dirty)
      }.padding(20).background(.bar)
      Divider()
      HSplitView {
        VStack(alignment: .leading, spacing: 16) {
          HStack {
            Text("MISSIONS").font(.caption.weight(.semibold)).foregroundStyle(.secondary)
            Spacer()
            Button {
              missions.create()
            } label: {
              Image(systemName: "plus")
            }.help("Create mission")
          }
          Picker(
            "Mission",
            selection: Binding(get: { missions.selectedID }, set: { missions.select($0) })
          ) {
            Text("Choose mission").tag(UUID?.none)
            ForEach(missions.plans) { Text($0.name).tag(Optional($0.id)) }
          }.labelsHidden()
          Divider()
          ScrollView {
            VStack(spacing: 6) {
              ForEach(sections, id: \.self) { name in
                if let group = navigationGroup(name) {
                  Text(group.uppercased()).font(.system(size: 10, weight: .semibold))
                    .tracking(1).foregroundStyle(.secondary)
                    .frame(maxWidth: .infinity, alignment: .leading)
                    .padding(.top, name == "Overview" ? 0 : 14).padding(.horizontal, 9)
                }
                Button {
                  section = name
                  formError = nil
                } label: {
                  HStack {
                    Image(systemName: icon(name)).frame(width: 22)
                    Text(name).fontWeight(section == name ? .semibold : .regular)
                    Spacer()
                    if let count = navigationCount(name), count > 0 {
                      Text(count.formatted()).font(.caption.monospacedDigit())
                        .foregroundStyle(.secondary).padding(.horizontal, 6).padding(.vertical, 2)
                        .background(.quaternary, in: Capsule())
                    }
                  }.padding(9).contentShape(Rectangle())
                    .background(
                      section == name ? Color.accentColor.opacity(0.14) : Color.clear,
                      in: RoundedRectangle(cornerRadius: 6))
                }.buttonStyle(.plain)
              }
            }
          }
          Text("Changes made here update the shared map. Automations run while Helios is open.")
            .font(.caption).foregroundStyle(.secondary)
        }.padding(16).frame(minWidth: 185, idealWidth: 205, maxWidth: 245)
        VStack(alignment: .leading, spacing: 0) {
          if missions.plan == nil {
            ContentUnavailableView(
              "Create your first mission", systemImage: "square.stack.3d.up",
              description: Text(
                "Organize a place, connect dated evidence, and record what you know and what needs review."
              ))
            Button("Create mission") { missions.create() }.buttonStyle(.borderedProminent).padding()
          } else {
            HStack {
              VStack(alignment: .leading, spacing: 5) {
                Text(section).font(.title2.weight(.semibold))
                Text(sectionDescription).font(.callout).foregroundStyle(.secondary)
                  .fixedSize(horizontal: false, vertical: true)
              }
              Spacer()
              if ["Evidence", "Objects", "Notes"].contains(section) {
                TextField("Search \(section.lowercased())", text: $query).textFieldStyle(
                  .roundedBorder
                ).frame(width: 230)
              }
            }.padding(20)
            Divider()
            ScrollView {
              VStack(alignment: .leading, spacing: 20) {
                content
              }.padding(20).frame(maxWidth: .infinity, alignment: .leading)
            }
          }
        }.frame(minWidth: 600, maxWidth: .infinity, maxHeight: .infinity)
      }
      Divider()
      HStack {
        if let error = formError ?? missions.error {
          Label(error, systemImage: "exclamationmark.triangle").foregroundStyle(.orange)
        } else {
          Text(
            missions.pendingForms || !missions.reviewDrafts.isEmpty
              ? "Unsaved entry — use its Save button"
              : (missions.dirty ? "Unsaved mission changes" : missions.status)
          ).foregroundStyle(
            .secondary)
        }
        Spacer()
        Text("Local mission workspace").foregroundStyle(.secondary)
      }.font(.caption).padding(10).lineLimit(2)
    }.background(WorkspaceDesign.canvas)
      .groupBoxStyle(AnalystGroupBoxStyle())
      .frame(minWidth: 1000, minHeight: 680)
      .onAppear { missions.discardForms = { clearForms() } }
      .onChange(of: [
        areaName, areaPurpose, noteTitle, noteBody, noteReference, objectTitle, taskTitle,
      ]) { _, values in
        missions.pendingForms = values.contains { !$0.isEmpty }
      }
      .onChange(of: missions.selectedID) { _, _ in
        areaEditingID = nil
        noteEditingID = nil
        evidenceID = nil
        noteArea = nil
        noteEvidence = nil
        objectArea = nil
        objectEvidence = nil
        monitorArea = nil
        query = ""
        formError = nil
        detection.cancel()
      }
      .onChange(of: model.missionPickedCoordinate) { _, value in
        if let value {
          latitude = String(format: "%.6f", value.latitude)
          longitude = String(format: "%.6f", value.longitude)
        }
      }
      .onChange(of: model.sarSearchRegion) { _, region in if let region { useRegion(region) } }
  }
  private func navigationGroup(_ name: String) -> String? {
    switch name {
    case "Overview": "Plan"
    case "Evidence": "Analysis"
    case "Notes": "Coordination"
    default: nil
    }
  }
  private func navigationCount(_ name: String) -> Int? {
    switch name {
    case "Areas": missions.brief.areas.count
    case "Evidence": missions.plan?.items.count ?? 0
    case "Objects": missions.brief.observations.count
    case "Notes": missions.brief.notes.count
    case "Tasks": missions.brief.tasks.filter { !$0.done }.count
    default: nil
    }
  }
  private var sectionDescription: String {
    switch section {
    case "Areas": "Define the places and boundaries your mission needs to understand."
    case "Evidence": "Connect source material, inspect provenance, and compare acquisitions."
    case "Timeline": "Review dated evidence in order and identify gaps in coverage."
    case "Objects": "Review observations and model suggestions against their source evidence."
    case "Notes": "Record findings, uncertainty, and supporting references."
    case "Tasks": "Keep track of the next actions and their due dates."
    case "Automations": "Check for new acquisitions while Helios is open."
    case "Activity": "Trace changes made to this mission."
    default: "Your working picture: places, evidence, and the next decisions."
    }
  }
  private func clearForms() {
    areaEditingID = nil
    noteEditingID = nil
    areaName = ""
    areaPurpose = ""
    noteTitle = ""
    noteBody = ""
    noteReference = ""
    objectTitle = ""
    taskTitle = ""
  }
  private func icon(_ value: String) -> String {
    switch value {
    case "Overview": "square.grid.2x2"
    case "Areas": "map"
    case "Evidence": "photo.on.rectangle"
    case "Timeline": "calendar.badge.clock"
    case "Objects": "scope"
    case "Notes": "note.text"
    case "Tasks": "checklist"
    case "Automations": "clock.arrow.circlepath"
    default: "clock"
    }
  }
  @ViewBuilder private var content: some View {
    switch section {
    case "Areas": areas
    case "Evidence": evidence
    case "Timeline":
      MissionTimelineView(model: model, missions: missions, optical: optical, sar: sar)
    case "Objects": objects
    case "Notes": notes
    case "Tasks": tasks
    case "Automations": monitors
    case "Activity": activity
    default: overview
    }
  }
  private var overview: some View {
    VStack(alignment: .leading, spacing: 20) {
      LazyVGrid(
        columns: Array(repeating: GridItem(.flexible(), spacing: 12), count: 4), spacing: 12
      ) {
        MissionSummaryCard(title: "Areas", count: missions.brief.areas.count, icon: "map") {
          section = "Areas"
        }
        MissionSummaryCard(
          title: "Evidence", count: missions.plan?.items.count ?? 0, icon: "photo.on.rectangle"
        ) {
          section = "Evidence"
        }
        MissionSummaryCard(
          title: "Needs review",
          count: missions.brief.observations.filter { $0.state == .unreviewed }.count, icon: "scope"
        ) {
          section = "Objects"
        }
        MissionSummaryCard(
          title: "Open tasks", count: missions.brief.tasks.filter { !$0.done }.count,
          icon: "checklist"
        ) {
          section = "Tasks"
        }
      }
      if missions.brief.areas.isEmpty {
        GroupBox("Start with a place") {
          Text(
            "Define an area of interest, connect dated imagery, then record your findings and the questions still open."
          )
          .foregroundStyle(.secondary)
          Button("Define an area", systemImage: "plus") { section = "Areas" }
            .buttonStyle(.borderedProminent)
        }
      }
      if !missions.brief.tasks.filter({ !$0.done }).isEmpty {
        GroupBox("Next actions") {
          VStack(alignment: .leading, spacing: 10) {
            ForEach(Array(missions.brief.tasks.filter { !$0.done }.prefix(3))) { task in
              HStack {
                Image(systemName: "circle").foregroundStyle(.secondary)
                Text(task.title).lineLimit(2)
                Spacer()
                Button("Open task") { section = "Tasks" }
              }
            }
          }
        }
      }
      GroupBox("Mission brief") {
        VStack(alignment: .leading, spacing: 10) {
          TextField("Mission name", text: $missions.name).textFieldStyle(.roundedBorder)
          Text("Objective").font(.callout.weight(.medium))
          TextEditor(text: $missions.objective).frame(minHeight: 65, maxHeight: 100)
            .overlay(RoundedRectangle(cornerRadius: 4).strokeBorder(WorkspaceDesign.border))
          DisclosureGroup("Working notes") {
            TextEditor(text: $missions.notes).frame(minHeight: 100)
              .overlay(RoundedRectangle(cornerRadius: 4).strokeBorder(WorkspaceDesign.border))
              .padding(.top, 6)
          }
        }
      }
      GroupBox("Map bookmarks") {
        VStack(alignment: .leading, spacing: 12) {
          HStack {
            TextField("Name this map position", text: $missions.itemName).textFieldStyle(
              .roundedBorder)
            Button("Save current view", systemImage: "bookmark") { model.addMissionView() }
          }
          Text(
            String(
              format: "Current map: %.5f°, %.5f°", model.camera.center.latitude,
              model.camera.center.longitude)
          )
          .font(.caption).foregroundStyle(.secondary).monospacedDigit()
          ForEach(missions.plan?.items.filter { $0.kind == .view } ?? []) { item in
            HStack {
              Label(item.title, systemImage: "mappin")
              Spacer()
              Button("Show on map") { show(item) }
            }
          }
        }
      }
    }
  }
  private var areas: some View {
    VStack(alignment: .leading, spacing: 16) {
      GroupBox("Define an area of interest") {
        VStack(alignment: .leading, spacing: 10) {
          TextField("Area name", text: $areaName)
          TextField("Purpose / what needs to be understood", text: $areaPurpose)
          HStack {
            TextField("West longitude", text: $west)
            TextField("South latitude", text: $south)
            TextField("East longitude", text: $east)
            TextField("North latitude", text: $north)
          }
          HStack {
            Button("Draw on map") {
              model.beginSarRegionSelection()
              model.missionAreaCapture = true
              model.revealMap?()
            }
            Button("Use selected region") { if let r = model.sarSearchRegion { useRegion(r) } }
              .disabled(model.sarSearchRegion == nil)
            Spacer()
            Button(areaEditingID == nil ? "Save area" : "Update area") { saveArea() }.buttonStyle(
              .borderedProminent
            ).disabled(
              areaName.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty)
          }
          Text("Decimal degrees · WGS84 · up to 10° per axis. Select two opposite map corners.")
            .font(.caption).foregroundStyle(.secondary)
        }.textFieldStyle(.roundedBorder).padding(10)
      }
      ForEach(missions.brief.areas) { area in
        GroupBox {
          VStack(alignment: .leading, spacing: 8) {
            HStack {
              Text(area.name).font(.headline)
              Spacer()
              Button("Show on map") { model.showMissionArea(area) }
            }
            Text(area.purpose).foregroundStyle(.secondary)
            Text(area.region.summary).font(.caption).monospacedDigit()
            HStack {
              Button("Edit") {
                areaEditingID = area.id
                areaName = area.name
                areaPurpose = area.purpose
                useRegion(area.region)
              }
              Button("Remove…") { missions.removeArea(area.id) }
              Button("Find imagery") {
                model.showMissionArea(area)
                model.openPanel(.satellite)
              }
              Button("Monitor acquisitions") {
                monitorArea = area.id
                section = "Automations"
              }
              Button("Add note") {
                noteArea = area.id
                section = "Notes"
              }
            }
          }.padding(8)
        }
      }
      if !satellite.sites.isEmpty {
        Text("Existing saved sites").font(.headline)
        ForEach(satellite.sites) { site in
          HStack {
            Text(site.name)
            Spacer()
            Button("Add as mission area") {
              missions.addArea(name: site.name, purpose: site.kind, region: site.region)
            }
          }
        }
      }
    }
  }
  private func useRegion(_ r: SentinelRegion) {
    west = String(r.west)
    south = String(r.south)
    east = String(r.east)
    north = String(r.north)
  }
  private func saveArea() {
    do {
      guard let w = Double(west), let s = Double(south), let e = Double(east), let n = Double(north)
      else { throw SentinelError.region }
      let r = try SentinelRegion(west: w, south: s, east: e, north: n)
      var saved = false
      if let id = areaEditingID {
        var area = MissionArea(name: areaName, purpose: areaPurpose, region: r)
        area.id = id
        saved = missions.updateArea(area)
      } else {
        saved = missions.addArea(name: areaName, purpose: areaPurpose, region: r)
      }
      if saved {
        areaEditingID = nil
        areaName = ""
        areaPurpose = ""
      }
      formError = nil
    } catch {
      formError =
        "Enter valid decimal bounds: west < east, south < north, with at most 10° per axis."
    }
  }
  private var evidence: some View {
    VStack(alignment: .leading, spacing: 16) {
      HStack {
        Text("Linked evidence").font(.headline)
        Spacer()
        Button("Satellite library") {
          model.openPanel(.satellite)
          satellite.page = "Library"
          model.revealMap?()
        }
        Button("Link inspected record") { model.addMissionRecord() }.disabled(model.selected == nil)
      }
      ForEach(
        (missions.plan?.items ?? []).filter {
          query.isEmpty || $0.title.localizedCaseInsensitiveContains(query)
        }
      ) { item in
        HStack(alignment: .top) {
          Image(
            systemName: item.kind == .radar ? "antenna.radiowaves.left.and.right" : "doc.richtext")
          VStack(alignment: .leading, spacing: 4) {
            Button(item.title) { evidenceID = item.id }.buttonStyle(.plain)
            Text(
              "\(item.kind.rawValue.capitalized) · linked \(item.savedAt.formatted(date: .abbreviated, time: .shortened))"
            )
            .font(.caption).foregroundStyle(.secondary)
            if !model.missionEvidenceAvailable(item) {
              Text("Source file missing from local library").font(.caption).foregroundStyle(.orange)
            }
          }
          Spacer()
          Button("Details") { evidenceID = evidenceID == item.id ? nil : item.id }
          Button("Map") { show(item) }
          Button("Note") {
            noteEvidence = item.id
            section = "Notes"
          }
          Button {
            missions.remove(item.id)
          } label: {
            Image(systemName: "minus.circle")
          }.help("Remove link; keep source data and notes")
        }.padding(12).background(
          evidenceID == item.id ? Color.accentColor.opacity(0.1) : Color.gray.opacity(0.05),
          in: RoundedRectangle(cornerRadius: 7))
      }
      if let item = missions.plan?.items.first(where: { $0.id == evidenceID }) {
        if let url = preview(item) { MissionImagePreview(url: url).frame(height: 260) }
        DisclosureGroup("Processing metadata / raw data") {
          ScrollView([.vertical, .horizontal]) {
            Text(metadata(item)).font(.system(.caption, design: .monospaced)).textSelection(
              .enabled
            )
            .frame(maxWidth: .infinity, alignment: .leading).padding(8)
          }.frame(height: 240).background(Color(nsColor: .textBackgroundColor))
        }
      }
      Divider()
      DisclosureGroup("Saved sites and map annotations") {
        VStack(alignment: .leading, spacing: 12) {
          Button("Open map annotations") {
            model.toolTab = "Annotations"
            model.openPanel(.tools)
            model.revealMap?()
          }
          ForEach(satellite.sites) { site in
            HStack {
              Text(site.name)
              Spacer()
              Button("Link site") {
                satellite.siteID = site.id
                model.addMissionSite()
              }
              .disabled(linked(.site, site.id.uuidString))
            }
          }
          ForEach(model.annotations) { annotation in
            HStack {
              Text(annotation.title)
              Spacer()
              Button("Link annotation") {
                var item = MissionItem(
                  kind: .annotation, referenceID: annotation.id.uuidString, title: annotation.title)
                item.annotation = annotation
                missions.add(item)
              }.disabled(linked(.annotation, annotation.id.uuidString))
            }
          }
        }.padding(.top, 10)
      }
      Text("Available optical images").font(.headline)
      ForEach(
        optical.images.filter { query.isEmpty || $0.title.localizedCaseInsensitiveContains(query) }
      ) { image in
        HStack {
          Text(image.title).font(.callout)
          Spacer()
          Button("Link") {
            optical.imageID = image.id
            model.addMissionOptical()
          }
          .disabled(linked(.optical, image.id.uuidString))
        }
      }
      Text("Processed radar acquisitions").font(.headline)
      ForEach(
        sar.jobs.filter {
          $0.result.state == .completed
            && (query.isEmpty || $0.title.localizedCaseInsensitiveContains(query))
        }
      ) { job in
        HStack {
          Text(job.title).font(.callout)
          Spacer()
          Button("Link") {
            sar.selectedJob = job.id
            model.addMissionRadar()
          }.disabled(linked(.radar, job.id))
        }
      }
      Divider()
      MissionComparisonView(model: model, missions: missions, optical: optical, sar: sar)
    }
  }
  private func linked(_ kind: MissionItemKind, _ id: String) -> Bool {
    missions.plan?.items.contains { $0.kind == kind && $0.referenceID == id } ?? false
  }
  private func show(_ item: MissionItem) {
    model.openMissionItem(item)
    model.revealMap?()
  }
  private func preview(_ item: MissionItem) -> URL? {
    if item.kind == .optical {
      return optical.images.first { $0.id.uuidString == item.referenceID }?.directory
        .appendingPathComponent("preview.png")
    }
    if item.kind == .radar, let job = sar.jobs.first(where: { $0.id == item.referenceID }),
      let output = job.result.outputs.first
    {
      return output.file(output.preview, in: job.directory)
    }
    return nil
  }
  private func metadata(_ item: MissionItem) -> String {
    let encoder = JSONEncoder()
    encoder.outputFormatting = [.prettyPrinted, .sortedKeys]
    encoder.dateEncodingStrategy = .iso8601
    let data: Data?
    if item.kind == .optical,
      let image = optical.images.first(where: { $0.id.uuidString == item.referenceID })
    {
      data = try? encoder.encode(image.result)
    } else if item.kind == .radar, let job = sar.jobs.first(where: { $0.id == item.referenceID }) {
      data = try? encoder.encode(job.result)
    } else {
      data = try? encoder.encode(item)
    }
    return data.map { String(decoding: $0, as: UTF8.self) } ?? "Metadata unavailable"
  }
  private var notes: some View {
    VStack(alignment: .leading, spacing: 16) {
      GroupBox("New evidence note") {
        VStack(alignment: .leading, spacing: 10) {
          TextField("Title", text: $noteTitle).textFieldStyle(.roundedBorder)
          HStack {
            areaPicker("Area", value: $noteArea)
            evidencePicker("Evidence", value: $noteEvidence)
          }
          TextEditor(text: $noteBody).frame(height: 140).border(.quaternary)
          TextField("Reference URL or citation", text: $noteReference).textFieldStyle(
            .roundedBorder)
          Button(noteEditingID == nil ? "Save note" : "Update note") {
            var note = MissionNote(title: noteTitle, body: noteBody)
            note.areaID = noteArea
            note.evidenceID = noteEvidence
            note.reference = noteReference
            var saved = false
            if let id = noteEditingID,
              let previous = missions.brief.notes.first(where: { $0.id == id })
            {
              note.id = id
              note.createdAt = previous.createdAt
              saved = missions.updateNote(note)
            } else {
              saved = missions.addNote(note)
            }
            if saved {
              noteEditingID = nil
              noteTitle = ""
              noteBody = ""
              noteReference = ""
            }
          }.buttonStyle(.borderedProminent).disabled(
            noteTitle.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty)
        }.padding(10)
      }
      ForEach(
        missions.brief.notes.filter {
          query.isEmpty || ($0.title + $0.body).localizedCaseInsensitiveContains(query)
        }
      ) { note in
        GroupBox {
          VStack(alignment: .leading, spacing: 8) {
            HStack {
              Text(note.title).font(.headline)
              Spacer()
              Text(note.createdAt.formatted()).font(.caption).foregroundStyle(.secondary)
            }
            Text(note.body).textSelection(.enabled)
            if !note.reference.isEmpty {
              Text(note.reference).font(.caption).textSelection(.enabled)
            }
            HStack {
              context(area: note.areaID, evidence: note.evidenceID)
              Spacer()
              Button("Delete", role: .destructive) {
                if missions.removeEntry(.note, id: note.id), noteEditingID == note.id {
                  noteEditingID = nil
                  noteTitle = ""
                  noteBody = ""
                  noteReference = ""
                }
              }
              Button("Edit note") {
                noteEditingID = note.id
                noteTitle = note.title
                noteBody = note.body
                noteReference = note.reference
                noteArea = note.areaID
                noteEvidence = note.evidenceID
              }
            }
          }.padding(8).frame(maxWidth: .infinity, alignment: .leading)
        }
      }
    }
  }
  private var tasks: some View {
    VStack(alignment: .leading, spacing: 16) {
      HStack {
        TextField("Next action", text: $taskTitle).textFieldStyle(.roundedBorder)
        Toggle("Due", isOn: $taskHasDue)
        if taskHasDue { DatePicker("Date", selection: $taskDue).labelsHidden() }
        Button("Add task") {
          if missions.addTask(MissionTask(title: taskTitle, due: taskHasDue ? taskDue : nil)) {
            taskTitle = ""
          }
        }.disabled(taskTitle.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty)
      }
      ForEach(missions.brief.tasks.sorted { !$0.done && $1.done }) { task in
        HStack {
          Button(role: .destructive) {
            _ = missions.removeEntry(.task, id: task.id)
          } label: {
            Image(systemName: "trash")
          }.help("Delete task")
          Toggle(
            task.title,
            isOn: Binding(get: { task.done }, set: { missions.setTask(task.id, done: $0) }))
          Spacer()
          if let due = task.due {
            Text(due.formatted()).foregroundStyle(
              !task.done && due < Date() ? Color.orange : Color.secondary
            ).font(.caption)
          }
        }.padding(12).background(.quaternary.opacity(0.2), in: RoundedRectangle(cornerRadius: 6))
      }
    }
  }
  private var monitors: some View {
    VStack(alignment: .leading, spacing: 16) {
      Text(
        "Scheduled checks run locally while Helios is open. Closing the mission window keeps them running. Reopening the app runs one overdue check per enabled automation."
      ).foregroundStyle(.secondary)
      GroupBox("Create automation") {
        VStack(alignment: .leading, spacing: 12) {
          Picker("Check", selection: $monitorKind) {
            ForEach(MissionMonitorKind.allCases, id: \.self) { Text($0.rawValue).tag($0) }
          }
          if monitorKind != .evidence { areaPicker("Area", value: $monitorArea) }
          Stepper("Every \(monitorInterval) hours", value: $monitorInterval, in: 1...168)
          if monitorKind == .sentinel2 {
            HStack {
              Text("Maximum tile cloud (%)")
              TextField("Cloud", value: $monitorCloud, format: .number).frame(width: 70)
            }
          }
          Text(
            "Acquisition checks query the last 14 days. The first run records a baseline; later runs flag newly seen catalogue entries. Large products download only when you choose them."
          ).font(.caption).foregroundStyle(.secondary)
          Button("Create paused automation") {
            var m = MissionMonitor(
              kind: monitorKind, areaID: monitorKind == .evidence ? nil : monitorArea)
            m.intervalHours = monitorInterval
            m.maximumCloud = monitorCloud
            _ = missions.addMonitor(m)
          }.disabled(monitorKind != .evidence && monitorArea == nil)
        }.padding(10)
      }
      ForEach(missions.brief.monitors) { monitor in
        GroupBox {
          VStack(alignment: .leading, spacing: 10) {
            HStack {
              Text(monitor.kind.rawValue).font(.headline)
              Button(role: .destructive) {
                _ = missions.removeEntry(.automation, id: monitor.id)
              } label: {
                Image(systemName: "trash")
              }.help("Delete automation")
              Spacer()
              Toggle(
                "Enabled",
                isOn: Binding(
                  get: { monitor.enabled }, set: { missions.setMonitor(monitor.id, enabled: $0) }))
              Button(automation.running.contains(monitor.id) ? "Running…" : "Run now") {
                if let id = missions.selectedID {
                  automation.run(missionID: id, monitorID: monitor.id)
                }
              }.disabled(automation.running.contains(monitor.id) || automation.running.count >= 2)
            }
            if let area = missions.brief.areas.first(where: { $0.id == monitor.areaID }) {
              Text(area.name).font(.subheadline)
            }
            Text(monitor.summary)
            if let failure = monitor.failure {
              Text(failure).foregroundStyle(.orange).font(.caption)
            }
            Text(
              "Every \(monitor.intervalHours)h · last success: \(monitor.lastSuccess?.formatted() ?? "Never")"
            ).font(.caption).foregroundStyle(.secondary)
            if monitor.kind != .evidence {
              Button("Review acquisitions on map") { model.openMonitorResults(monitor) }
              ForEach(automation.matches[monitor.id] ?? []) { match in
                Text("\(match.acquiredAt) · \(match.title)").font(.caption).textSelection(.enabled)
              }
            }
          }.padding(10)
        }
      }
    }
  }
  private var objects: some View {
    VStack(alignment: .leading, spacing: 16) {
      GroupBox("Record an object identification") {
        VStack(alignment: .leading, spacing: 10) {
          Text(
            "Keep the source image, observation time and review judgment attached to every identification."
          ).font(.caption).foregroundStyle(.secondary)
          HStack {
            TextField("Object name", text: $objectTitle)
            TextField("Category", text: $objectCategory)
          }.textFieldStyle(.roundedBorder)
          HStack {
            areaPicker("Area", value: $objectArea)
            evidencePicker("Evidence", value: $objectEvidence)
          }
          HStack {
            TextField("Latitude", text: $latitude)
            TextField("Longitude", text: $longitude)
            Button("Pick on map") {
              model.missionPick = true
              model.drawing = false
              model.routeEndpoint = nil
              model.cancelSarRegionSelection()
              model.revealMap?()
            }
            Button("Use map center") {
              latitude = String(model.camera.center.latitude)
              longitude = String(model.camera.center.longitude)
            }
          }.textFieldStyle(.roundedBorder)
          Button("Save observation") { saveObservation() }.buttonStyle(.borderedProminent).disabled(
            objectTitle.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty)
        }.padding(10)
      }
      GroupBox("Local model assistance") {
        VStack(alignment: .leading, spacing: 10) {
          Text(
            "Use a Vision-compatible Core ML object detector with linked optical imagery. Model suitability and source pixel size determine which objects can be resolved. Scores are model outputs, not calibrated probabilities."
          ).font(.caption).foregroundStyle(.secondary)
          DisclosureGroup("Choose a model for your imagery") {
            VStack(alignment: .leading, spacing: 8) {
              Text(
                "Current adapter: Core ML models returning Vision object boxes (.mlmodel, .mlpackage or .mlmodelc). PyTorch .pt files and raw rotated-box outputs require another adapter."
              )
              Text(
                "Compatibility trial: Apple's YOLOv3 FP16 is a documented Core ML object detector trained for everyday objects. It is not an aerial infrastructure model; inference and accuracy in Helios still need validation."
              )
              Link(
                "Apple YOLOv3 models",
                destination: URL(string: "https://huggingface.co/apple/coreml-YOLOv3")!)
              Text(
                "Aerial evaluation candidate: YOLO11s-OBB, trained for overhead classes including ships, aircraft, bridges and storage tanks. Rotated-box integration is not implemented. Its DOTA classes do not include generic buildings or damage; review AGPL/commercial licensing before distribution."
              )
              Link(
                "YOLO oriented object detection",
                destination: URL(string: "https://docs.ultralytics.com/tasks/obb/")!)
              Text(
                "10 m Sentinel-2 pixels cannot resolve small vehicles or individual small buildings. Use sufficiently detailed optical imagery and evaluate against labeled examples from the same source."
              )
            }.font(.caption).foregroundStyle(.secondary).padding(.vertical, 6)
          }
          HStack {
            Button("Choose detector…") { detection.chooseModel() }.disabled(detection.busy)
            Text(detection.modelURL?.lastPathComponent ?? "No detector selected").font(.caption)
            Spacer()
            Text("Minimum score")
            TextField("Score", value: $detection.minimumScore, format: .number).frame(width: 65)
          }
          evidencePicker("Optical evidence to analyze", value: $objectEvidence, opticalOnly: true)
          HStack {
            Button("Analyze image") { analyze() }.disabled(
              detection.busy || detection.modelURL == nil || objectEvidence == nil
                || !(0...1).contains(detection.minimumScore))
            if detection.busy {
              ProgressView().controlSize(.small)
              Button("Cancel") { detection.cancel() }
            }
            if !detection.proposals.isEmpty && detection.missionID == missions.selectedID {
              Button("Add \(detection.proposals.count) proposals for review") {
                if missions.editBrief(
                  "Added \(detection.proposals.count) unreviewed model proposals",
                  { $0.observations.append(contentsOf: detection.proposals) })
                {
                  detection.cancel()
                }
              }
            }
          }
          if let error = detection.error { Text(error).font(.caption).foregroundStyle(.orange) }
          if !detection.busy, detection.missionID != nil, detection.proposals.isEmpty,
            detection.error == nil
          {
            Text(
              "No proposals above the selected score. This does not establish that no objects are present."
            ).font(.caption)
          }
        }.padding(10)
      }
      ForEach(
        missions.brief.observations.filter {
          query.isEmpty
            || ($0.title + $0.category + $0.note).localizedCaseInsensitiveContains(query)
        }
      ) { observation in
        MissionObservationCard(observation: observation, model: model, missions: missions)
      }
    }
  }
  private func analyze() {
    guard let id = missions.selectedID,
      let item = missions.plan?.items.first(where: {
        $0.id == objectEvidence && $0.kind == .optical
      }),
      let image = optical.images.first(where: { $0.id.uuidString == item.referenceID })
    else {
      formError = "Choose a linked optical image that is still available locally."
      return
    }
    detection.run(image: image, evidenceID: item.id, missionID: id)
  }
  private func saveObservation() {
    guard let lat = Double(latitude), let lon = Double(longitude),
      let coordinate = Coordinate(latitude: lat, longitude: lon)
    else {
      formError = "Enter valid latitude and longitude or pick a point on the map."
      return
    }
    var item = MissionObservation(
      title: objectTitle, category: objectCategory, coordinate: coordinate)
    item.areaID = objectArea
    item.evidenceID = objectEvidence
    if let evidence = missions.plan?.items.first(where: { $0.id == objectEvidence }) {
      if let image = optical.images.first(where: { $0.id.uuidString == evidence.referenceID }) {
        item.observedAt = image.result.scene.flatMap { ProviderParsers.date($0.acquiredAt) }
      } else if let metadata = sar.jobs.first(where: { $0.id == evidence.referenceID })?.result
        .metadata
      {
        item.observedAt = ProviderParsers.date(metadata.acquisitionStart)
      } else {
        item.observedAt = evidence.record?.observedAt
      }
    }
    if missions.addObservation(item) {
      objectTitle = ""
      formError = nil
    }
  }
  private var activity: some View {
    VStack(alignment: .leading, spacing: 14) {
      Text(
        "Recent local activity · up to 200 events · this is a working history, not a tamper-proof audit log."
      ).font(.caption).foregroundStyle(.secondary)
      ForEach(missions.brief.activity) { event in
        HStack(alignment: .top) {
          Text(event.time.formatted()).font(.caption).foregroundStyle(.secondary).frame(
            width: 180, alignment: .leading)
          Text(event.message)
          Spacer()
        }
        Divider()
      }
    }
  }
  private func areaPicker(_ title: String, value: Binding<UUID?>) -> some View {
    Picker(title, selection: value) {
      Text("No area").tag(UUID?.none)
      ForEach(missions.brief.areas) { Text($0.name).tag(Optional($0.id)) }
    }
  }
  private func evidencePicker(_ title: String, value: Binding<UUID?>, opticalOnly: Bool = false)
    -> some View
  {
    Picker(title, selection: value) {
      Text("No evidence").tag(UUID?.none)
      ForEach((missions.plan?.items ?? []).filter { !opticalOnly || $0.kind == .optical }) {
        Text($0.title).tag(Optional($0.id))
      }
    }
  }
  @ViewBuilder private func context(area: UUID?, evidence: UUID?) -> some View {
    HStack {
      if let value = missions.brief.areas.first(where: { $0.id == area }) {
        Button(value.name) { model.showMissionArea(value) }
      }
      if let value = missions.plan?.items.first(where: { $0.id == evidence }) {
        Button("Open evidence") { show(value) }
      }
    }
  }
}

struct MissionObservationCard: View {
  let observation: MissionObservation
  @ObservedObject var model: WorkspaceModel
  @ObservedObject var missions: MissionWorkspace
  private var state: MissionReviewState {
    missions.reviewDrafts[observation.id]?.0 ?? observation.state
  }
  private var note: String { missions.reviewDrafts[observation.id]?.1 ?? observation.note }
  var body: some View {
    GroupBox {
      VStack(alignment: .leading, spacing: 10) {
        HStack {
          Text(observation.title).font(.headline)
          Text(observation.category).foregroundStyle(.secondary)
          Spacer()
          Button("Show on map") { model.showObservation(observation) }
          Button(role: .destructive) {
            if missions.removeEntry(.observation, id: observation.id),
              model.missionFocus?.id == "mission:\(observation.id)"
            {
              model.missionFocus = nil
              model.reviewOutline = []
            }
          } label: {
            Image(systemName: "trash")
          }.help("Delete observation")
        }
        Text(
          "\(observation.method) · observed: \(observation.observedAt?.formatted() ?? "Unknown")"
        ).font(.caption).foregroundStyle(.secondary)
        if let score = observation.modelScore {
          Text(String(format: "Model score %.3f · uncalibrated", score)).font(.caption)
        }
        HStack {
          Picker(
            "Review",
            selection: Binding(
              get: { state }, set: { missions.reviewDrafts[observation.id] = ($0, note) })
          ) {
            ForEach(MissionReviewState.allCases, id: \.self) { Text($0.rawValue).tag($0) }
          }
          TextField(
            "Review rationale / description",
            text: Binding(
              get: { note }, set: { missions.reviewDrafts[observation.id] = (state, $0) })
          ).textFieldStyle(.roundedBorder)
          Button("Save review") {
            if missions.reviewObservation(observation.id, state: state, note: note) {
              missions.reviewDrafts[observation.id] = nil
            }
          }
          .disabled(state == observation.state && note == observation.note)
        }
      }.padding(10)
    }
  }
}
struct MissionImagePreview: View {
  let url: URL
  var maximumPixelSize = 1024
  var compact = false
  @State private var image: CGImage?
  var body: some View {
    Group {
      if let image {
        Image(decorative: image, scale: 1).resizable().interpolation(.none).scaledToFit()
      } else if compact {
        Image(systemName: "photo").foregroundStyle(.secondary)
          .accessibilityLabel("Preview unavailable")
      } else {
        ContentUnavailableView("Preview unavailable", systemImage: "photo")
      }
    }.task(id: url) {
      image = nil
      let thumbnail = await Task.detached {
        guard let size = try? url.resourceValues(forKeys: [.fileSizeKey]).fileSize,
          size <= 32 * 1024 * 1024,
          let source = CGImageSourceCreateWithURL(url as CFURL, nil)
        else { return CGImage?.none }
        return CGImageSourceCreateThumbnailAtIndex(
          source, 0,
          [
            kCGImageSourceCreateThumbnailFromImageAlways: true,
            kCGImageSourceThumbnailMaxPixelSize: maximumPixelSize,
          ] as CFDictionary)
      }.value
      guard !Task.isCancelled else { return }
      image = thumbnail
    }
  }
}
struct MissionComparisonView: View {
  @ObservedObject var model: WorkspaceModel
  @ObservedObject var missions: MissionWorkspace
  @ObservedObject var optical: OpticalWorkspace
  @ObservedObject var sar: SarWorkspace
  var body: some View {
    GroupBox("Compare acquisitions on the map") {
      VStack(alignment: .leading, spacing: 12) {
        Picker("A · baseline", selection: $missions.baseID) {
          Text("Choose layer").tag("")
          ForEach(model.rasterChoices) { Text($0.title).tag($0.id) }
        }
        Picker("B · comparison", selection: $missions.topID) {
          Text("Choose layer").tag("")
          ForEach(model.rasterChoices) { Text($0.title).tag($0.id) }
        }
        if [missions.baseID, missions.topID].contains(where: { id in
          !id.isEmpty && !model.rasterChoices.contains(where: { $0.id == id })
        }) {
          Text("A saved comparison layer is unavailable. Select an available image.")
            .foregroundStyle(.orange)
        }
        HStack {
          Toggle("Display A/B", isOn: $missions.comparisonEnabled)
            .disabled(
              !missions.comparisonEnabled
                && (!model.rasterChoices.contains(where: { $0.id == missions.baseID })
                  || !model.rasterChoices.contains(where: { $0.id == missions.topID })
                  || missions.baseID == missions.topID)
            )
          Slider(value: $missions.blend, in: 0...1)
          Text("B \(Int(missions.blend*100))%")
          Button("Show map") {
            if let a = model.rasterChoices.first(where: { $0.id == missions.baseID }) {
              model.focusSatelliteBounds(a.overlay.bounds)
            }
            model.revealMap?()
          }
        }
        if let a = model.rasterChoices.first(where: { $0.id == missions.baseID }),
          let b = model.rasterChoices.first(where: { $0.id == missions.topID }),
          !RasterComparison.overlaps(a.overlay.bounds, b.overlay.bounds)
        {
          Text("These layers do not overlap geographically.").foregroundStyle(.orange)
        }
        Text(
          "Visual comparison of dated previews. Different sensors, clouds, viewing geometry and display stretches can cause apparent changes."
        ).font(.caption).foregroundStyle(.secondary)
        Button("Measured SAR change review") { satelliteReview() }
      }.padding(10)
    }
  }
  private func satelliteReview() {
    model.satellite.page = "Infrastructure review"
    model.openPanel(.satellite)
    model.revealMap?()
  }
}
