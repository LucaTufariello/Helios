import AppKit
import GodEyeCore
import SwiftUI

struct MissionPlannerView: View {
  @ObservedObject var model: WorkspaceModel
  @ObservedObject var missions: MissionWorkspace
  @ObservedObject var satellite: SatelliteWorkspace
  @ObservedObject var optical: OpticalWorkspace
  @ObservedObject var sar: SarWorkspace
  @State private var tab = "Plan"
  var body: some View {
    VStack(alignment: .leading, spacing: 12) {
      HStack {
        Picker(
          "Mission", selection: Binding(get: { missions.selectedID }, set: { missions.select($0) })
        ) {
          Text("Select mission").tag(UUID?.none)
          ForEach(missions.plans) { Text($0.name).tag(Optional($0.id)) }
        }
        Button {
          missions.create()
        } label: {
          Image(systemName: "plus")
        }.help("New mission")
          .accessibilityIdentifier("new-mission")
      }.padding(.horizontal, 14).padding(.top, 12)
      if let plan = missions.plan {
        Picker("Mission workspace", selection: $tab) {
          ForEach(["Plan", "Evidence", "Compare", "Notes"], id: \.self) { Text($0).tag($0) }
        }.pickerStyle(.segmented).padding(.horizontal, 14)
        ScrollView {
          VStack(alignment: .leading, spacing: 14) {
            switch tab {
            case "Evidence": evidence(plan)
            case "Compare": comparison
            case "Notes":
              Text("Mission notes").font(.headline)
              TextEditor(text: $missions.notes).frame(minHeight: 240)
                .font(.body).padding(4).background(.quaternary.opacity(0.2))
                .accessibilityIdentifier("mission-notes")
              Text("\(missions.notes.count) / 32,000 characters").font(.caption).foregroundStyle(
                .secondary)
              Button("Open map annotations") {
                model.toolTab = "Annotations"
                model.showTools = true
              }
              ForEach(model.annotations) { note in
                HStack {
                  Text(note.title).lineLimit(2)
                  Spacer()
                  Button("Link") {
                    var item = MissionItem(
                      kind: .annotation, referenceID: note.id.uuidString, title: note.title)
                    item.annotation = note
                    missions.add(item)
                  }
                }
              }
            default:
              TextField("Mission name", text: $missions.name).textFieldStyle(.roundedBorder)
              Text("Objective").font(.headline)
              TextEditor(text: $missions.objective).frame(minHeight: 80).padding(4)
                .background(.quaternary.opacity(0.2)).accessibilityIdentifier("mission-objective")
              Text("Saved locations").font(.headline)
              TextField("Location / view name", text: $missions.itemName).textFieldStyle(
                .roundedBorder)
              Button("Save current map view") { model.addMissionView() }
                .accessibilityIdentifier("save-mission-view")
              Text("Coordinates, range, heading and tilt are retained.").font(.caption)
                .foregroundStyle(.secondary)
              ForEach(
                plan.items.filter { $0.kind == .view || $0.kind == .site || $0.kind == .record }
              ) { item in
                itemRow(item)
              }
              Divider()
              HStack {
                Button("Satellite tools") { model.openPanel(.satellite) }
                Button("Infrastructure sources") { model.openPanel(.sources) }
              }
              HStack {
                Button("Map annotations") {
                  model.toolTab = "Annotations"
                  model.showTools = true
                }
                Button("Raw data") {
                  model.rawScope = "Mission"
                  model.openPanel(.raw)
                }
              }
              Text("\(plan.items.count) linked locations and evidence items").font(.caption)
                .foregroundStyle(.secondary)
            }
            if let error = missions.error { Text(error).font(.callout).foregroundStyle(.orange) }
          }.padding(14).frame(maxWidth: .infinity, alignment: .leading)
        }
        Divider()
        HStack {
          Text(missions.dirty ? "Unsaved changes" : missions.status).font(.caption).lineLimit(2)
          Spacer()
          Button("Export…") { missions.export() }
          Button("Save") { missions.save() }.buttonStyle(.borderedProminent)
            .accessibilityIdentifier("save-mission")
        }.padding(.horizontal, 14).padding(.bottom, 12)
      } else {
        ContentUnavailableView(
          "No mission selected", systemImage: "square.stack.3d.up",
          description: Text(
            "Create a mission to connect saved views, infrastructure, radar and optical imagery."))
      }
    }
  }
  private func evidence(_ plan: MissionPlan) -> some View {
    VStack(alignment: .leading, spacing: 12) {
      Text("Link selected evidence").font(.headline)
      Text(
        "Select a saved item in its tool panel, then link it here. Files stay in their existing libraries."
      )
      .font(.caption).foregroundStyle(.secondary)
      Button("Link selected site") { model.addMissionSite() }.disabled(satellite.site == nil)
      Button("Link inspected record") { model.addMissionRecord() }.disabled(model.selected == nil)
      Button("Link selected optical image") { model.addMissionOptical() }.disabled(
        optical.image == nil)
      Button("Link selected SAR acquisition") { model.addMissionRadar() }
        .disabled(sar.currentJob?.result.state != .completed)
      HStack {
        Button("Open satellite tools") { model.openPanel(.satellite) }
        Button("Inspect records") { model.openPanel(.inspector) }
      }
      Divider()
      ForEach(plan.items) { item in itemRow(item) }
      Text(
        "Saved record snapshots retain source time and attribution. They do not update as live targets move."
      )
      .font(.caption).foregroundStyle(.secondary)
    }
  }
  private func itemRow(_ item: MissionItem) -> some View {
    VStack(alignment: .leading, spacing: 5) {
      HStack {
        Image(systemName: icon(item.kind)).foregroundStyle(.secondary)
        Text(item.title).font(.callout.weight(.medium)).lineLimit(2)
        Spacer()
      }
      HStack {
        Text(item.kind.rawValue.capitalized).font(.caption).foregroundStyle(.secondary)
        Spacer()
        Button("Show on map") { model.openMissionItem(item) }
        Button {
          missions.remove(item.id)
        } label: {
          Image(systemName: "minus.circle")
        }
        .help("Remove link from mission; keep source data")
        .accessibilityLabel("Remove mission link \(item.title)")
      }
    }.padding(10).background(.quaternary.opacity(0.15), in: RoundedRectangle(cornerRadius: 6))
  }
  private func icon(_ kind: MissionItemKind) -> String {
    switch kind {
    case .view: "mappin.and.ellipse"
    case .site: "building.2"
    case .record: "scope"
    case .radar: "antenna.radiowaves.left.and.right"
    case .optical: "photo"
    case .annotation: "note.text"
    }
  }
  private var comparison: some View {
    VStack(alignment: .leading, spacing: 12) {
      Text("Compare saved imagery").font(.headline)
      Text(
        "Georeferenced layers remain on the map. Use the blend control or switch A/B to inspect differences."
      )
      .font(.callout)
      Picker("Base layer A", selection: $missions.baseID) {
        Text("Choose saved image").tag("")
        ForEach(model.rasterChoices) { Text($0.title).tag($0.id) }
      }
      Picker("Overlay B", selection: $missions.topID) {
        Text("Choose saved image").tag("")
        ForEach(model.rasterChoices) { Text($0.title).tag($0.id) }
      }
      Toggle("Show comparison layers", isOn: $missions.comparisonEnabled)
        .disabled(
          !missions.comparisonEnabled
            && (missions.baseID.isEmpty || missions.topID.isEmpty
              || missions.baseID == missions.topID)
        )
      if !missions.baseID.isEmpty || !missions.topID.isEmpty {
        let availableIDs = Set(model.rasterChoices.map(\.id))
        if (!missions.baseID.isEmpty && !availableIDs.contains(missions.baseID))
          || (!missions.topID.isEmpty && !availableIDs.contains(missions.topID))
        {
          Text("A saved comparison image is missing from the local library. Choose another layer.")
            .font(.caption).foregroundStyle(.orange)
        }
      }
      HStack {
        Text("A")
        Slider(value: $missions.blend, in: 0...1)
        Text("B \(Int(missions.blend * 100))%")
      }
      HStack {
        Button("Show A") { missions.blend = 0 }
        Button("Blend") { missions.blend = 0.5 }
        Button("Show B") { missions.blend = 1 }
        Button("Focus A") {
          if let choice = model.rasterChoices.first(where: { $0.id == missions.baseID }) {
            model.orient(heading: 0, tilt: 0)
            model.focusSatelliteBounds(choice.overlay.bounds)
          }
        }
      }
      if let a = model.rasterChoices.first(where: { $0.id == missions.baseID }),
        let b = model.rasterChoices.first(where: { $0.id == missions.topID }),
        !RasterComparison.overlaps(a.overlay.bounds, b.overlay.bounds)
      {
        Text("These layers have no overlapping geographic coverage.").font(.caption)
          .foregroundStyle(.orange)
      }
      Text(
        "Visual comparison only. Different sensors, acquisition geometry, clouds or display stretches can create apparent differences. Use infrastructure review for measured SAR change with compatible acquisition pairs."
      )
      .font(.caption).foregroundStyle(.secondary)
      Button("Open measured SAR change review") {
        satellite.page = "Infrastructure review"
        model.openPanel(.satellite)
      }
    }
  }
}

struct RawDataView: View {
  @ObservedObject var model: WorkspaceModel
  @State private var text = ""
  var body: some View {
    VStack(alignment: .leading, spacing: 10) {
      Picker("Data", selection: $model.rawScope) {
        ForEach(
          ["Selected record", "Optical image", "SAR acquisition", "Change review", "Mission"],
          id: \.self
        ) {
          Text($0).tag($0)
        }
      }.padding(.horizontal, 14).padding(.top, 12)
      Text("Read-only metadata snapshot · Refresh to capture current data").font(.caption)
        .foregroundStyle(.secondary).padding(.horizontal, 14)
      ScrollView([.vertical, .horizontal]) {
        Text(text).font(.system(size: 11, design: .monospaced)).textSelection(.enabled)
          .frame(maxWidth: .infinity, alignment: .leading).padding(14)
      }.background(Color(nsColor: .textBackgroundColor))
      HStack {
        Button("Refresh") { refresh() }
        Button("Copy JSON") {
          NSPasteboard.general.clearContents()
          NSPasteboard.general.setString(text, forType: .string)
        }
        Spacer()
      }.padding(.horizontal, 14).padding(.bottom, 12)
    }.onAppear { refresh() }.onChange(of: model.rawScope) { _, _ in refresh() }
  }
  private func refresh() {
    let encoder = JSONEncoder()
    encoder.outputFormatting = [.prettyPrinted, .sortedKeys]
    encoder.dateEncodingStrategy = .iso8601
    let data: Data?
    switch model.rawScope {
    case "Mission": data = model.missions.plan.flatMap { try? encoder.encode($0) }
    case "SAR acquisition": data = model.sar.currentJob.flatMap { try? encoder.encode($0.result) }
    case "Optical image": data = model.optical.image.flatMap { try? encoder.encode($0.result) }
    case "Change review": data = model.satellite.review.flatMap { try? encoder.encode($0.result) }
    default: data = model.selected.flatMap { try? encoder.encode(MissionRecord($0)) }
    }
    guard let data else {
      text = "No item selected for this data source."
      return
    }
    guard data.count <= 1024 * 1024 else {
      text = "Metadata exceeds the display limit. Use the source export."
      return
    }
    text = String(decoding: data, as: UTF8.self)
  }
}
