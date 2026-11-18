import AppKit
import HeliosCore
import SwiftUI

enum WorkspacePanel: String, CaseIterable, Identifiable {
  case sources, satellite, mission, inspector, raw, map, tools
  var id: String { rawValue }
  var title: String {
    switch self {
    case .sources: "Data sources"
    case .satellite: "Satellite analysis"
    case .mission: "Mission Control"
    case .inspector: "Inspector & records"
    case .raw: "Raw data"
    case .map: "Map & layers"
    case .tools: "Workspace tools"
    }
  }
  var icon: String {
    switch self {
    case .sources: "square.3.layers.3d"
    case .satellite: "antenna.radiowaves.left.and.right"
    case .mission: "square.stack.3d.up"
    case .inspector: "scope"
    case .raw: "curlybraces"
    case .map: "map"
    case .tools: "slider.horizontal.3"
    }
  }
  var minimumWidth: CGFloat {
    switch self {
    case .satellite, .tools: 500
    case .raw: 440
    case .inspector: 340
    default: 320
    }
  }
  var width: CGFloat {
    switch self {
    case .sources, .map: 350
    case .inspector: 390
    case .mission: 460
    case .raw, .tools: 620
    case .satellite: 550
    }
  }
}

struct WorkspaceView: View {
  @ObservedObject var model: WorkspaceModel
  var body: some View {
    GeometryReader { geometry in
      ZStack(alignment: .topLeading) {
        SarGlobe(
          model: model, sar: model.sar, satellite: model.satellite, optical: model.optical,
          missions: model.missions
        )
        .ignoresSafeArea()
        if let error = model.renderError {
          ContentUnavailableView(
            "Metal rendering unavailable", systemImage: "exclamationmark.triangle",
            description: Text(error))
        }
        VStack(spacing: 0) {
          navigation
          HStack {
            searchBar.frame(width: min(380, geometry.size.width * 0.32))
            Spacer()
            mapActions
          }.padding(.horizontal, 14).padding(.top, 10)
          Spacer()
          statusBar
        }
        if model.missionPick {
          HStack {
            Text("Click the map to place the object observation")
            Button("Cancel") {
              model.missionPick = false
              model.revealMission?()
            }
          }.padding(12).background(.regularMaterial, in: RoundedRectangle(cornerRadius: 8))
            .padding(.top, 108).padding(.leading, 14)
        }
        if model.drawing || model.routeEndpoint != nil {
          HStack {
            Text(
              model.drawing
                ? "Click the map to add annotation vertices"
                : "Click the map to place endpoint \(model.routeEndpoint ?? "")")
            Button("Finish…") {
              model.toolTab = model.drawing ? "Annotations" : "Directions"
              model.showTools = true
            }
            Button("Cancel") {
              model.drawing = false
              model.drawingPoints = []
              model.routeEndpoint = nil
            }
          }.font(.callout).padding(10).background(
            .regularMaterial, in: RoundedRectangle(cornerRadius: 8)
          )
          .padding(.top, 108).padding(.leading, 14)
        }
        ForEach(model.panels) { panel in
          FloatingWorkspacePanel(
            panel: panel, available: geometry.size,
            close: { model.closePanel(panel) },
            focus: { model.focusPanel(panel) },
            isFrontmost: model.panels.last == panel
          ) {
            panelContent(panel)
          }.zIndex(Double((model.panels.firstIndex(of: panel) ?? 0) + 1))
        }
      }
    }
    .sheet(item: $model.cameraRecord) { CameraViewer(record: $0) }
    .sheet(isPresented: $model.showAbout) { AboutView() }
    .onChange(of: model.showTools) { _, value in
      if value { model.openPanel(.tools) } else { model.closePanel(.tools) }
    }
    .onChange(of: model.selectedID) { _, id in
      if id != nil { model.openPanel(.inspector) }
    }
    .groupBoxStyle(AnalystGroupBoxStyle())
    .frame(minWidth: 1080, minHeight: 620)
  }
  private var navigation: some View {
    HStack(spacing: 6) {
      HStack(spacing: 7) {
        Image(systemName: "globe.europe.africa.fill").foregroundStyle(.tint)
        Text("Helios").font(.system(size: 15, weight: .semibold))
      }.padding(.leading, 4).padding(.trailing, 18)
      ForEach(["Map", "Satellite", "Marine", "Sky", "Ground"], id: \.self) {
        section in
        Button {
          model.showSection(section)
        } label: {
          Label(section, systemImage: sectionIcon(section)).font(.system(size: 12, weight: .medium))
            .padding(.horizontal, 12).padding(.vertical, 9)
            .foregroundStyle(model.workspaceSection == section ? Color.accentColor : Color.primary)
            .background(
              model.workspaceSection == section ? Color.accentColor.opacity(0.12) : Color.clear,
              in: RoundedRectangle(cornerRadius: 6))
        }.buttonStyle(.plain).accessibilityIdentifier("section-\(section)")
      }
      Spacer(minLength: 8)
      Text("\(model.enabled.count) sources on").font(.caption).foregroundStyle(.secondary)
      Divider().frame(height: 20).padding(.horizontal, 6)
      Button {
        model.showSection("Mission planner")
      } label: {
        Label("Mission Control", systemImage: "rectangle.on.rectangle")
          .font(.system(size: 12, weight: .semibold)).padding(.horizontal, 10).padding(.vertical, 8)
          .background(Color.accentColor.opacity(0.12), in: RoundedRectangle(cornerRadius: 6))
          .foregroundStyle(.tint)
      }.buttonStyle(.plain).help("Open the mission workspace in a separate window")
        .accessibilityIdentifier("section-Mission planner")
      Button {
        model.panels.removeAll()
        model.showTools = false
      } label: {
        Image(systemName: "rectangle.slash")
      }.help("Hide floating panels").accessibilityIdentifier("hide-panels")
    }.padding(.horizontal, 12).padding(.vertical, 6).background(.bar)
      .overlay(alignment: .bottom) { Divider() }
  }
  private func sectionIcon(_ section: String) -> String {
    switch section {
    case "Satellite": "antenna.radiowaves.left.and.right"
    case "Marine": "water.waves"
    case "Sky": "airplane"
    case "Ground": "building.2"
    default: "map"
    }
  }
  private var searchBar: some View {
    VStack(alignment: .leading, spacing: 0) {
      HStack(spacing: 8) {
        Image(systemName: "magnifyingglass").foregroundStyle(.secondary)
        TextField("Search locations…", text: $model.searchQuery).textFieldStyle(.plain)
          .onSubmit { model.search() }.accessibilityIdentifier("place-search")
        if model.searchLoading {
          ProgressView().controlSize(.small)
        } else {
          Button {
            model.search()
          } label: {
            Image(systemName: "arrow.right")
          }
          .buttonStyle(.plain).disabled(
            model.searchQuery.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty)
        }
      }.padding(10)
      if let error = model.searchError {
        Text(error).font(.caption).foregroundStyle(.orange).padding(8)
      }
      if !model.searchResults.isEmpty {
        Divider()
        ForEach(Array(model.searchResults.enumerated()), id: \.offset) { _, result in
          Button(result.0) {
            model.fly(to: result.1, altitude: 20_000)
            model.searchResults = []
          }
          .buttonStyle(.plain).padding(8)
        }
        Text("Photon / komoot · © OpenStreetMap").font(.caption2).foregroundStyle(.secondary)
          .padding(8)
      }
    }.background(.regularMaterial, in: RoundedRectangle(cornerRadius: 8))
  }
  private var mapActions: some View {
    HStack(spacing: 0) {
      toolButton("Layers", "square.3.layers.3d", .map)
      toolButton("Inspect", "scope", .inspector)
      toolButton("Data", "curlybraces", .raw)
      Menu {
        Button("All data sources") {
          model.workspaceSection = "Map"
          model.openPanel(.sources)
        }
        ForEach(["3D Surface", "Directions", "Annotations", "Providers", "Workspace"], id: \.self) {
          tool in
          Button(tool) {
            model.toolTab = tool
            model.showTools = true
          }
        }
        Button("About Helios") { model.showAbout = true }
      } label: {
        Image(systemName: "slider.horizontal.3").frame(width: 38, height: 34)
      }
      .menuStyle(.borderlessButton).fixedSize().help("Workspace tools")
      Divider().frame(height: 20)
      Button {
        model.zoom(delta: 8)
      } label: {
        Image(systemName: "plus.magnifyingglass").frame(width: 34, height: 34)
      }.help("Zoom in")
      Button {
        model.zoom(delta: -8)
      } label: {
        Image(systemName: "minus.magnifyingglass").frame(width: 34, height: 34)
      }.help("Zoom out")
      Button {
        model.resetGlobe()
      } label: {
        Image(systemName: "globe").frame(width: 34, height: 34)
      }.help("Full globe")
    }.buttonStyle(.plain).padding(.horizontal, 6).background(
      .regularMaterial, in: RoundedRectangle(cornerRadius: 8))
  }
  private func toolButton(_ title: String, _ icon: String, _ panel: WorkspacePanel) -> some View {
    Button {
      model.openPanel(panel)
    } label: {
      Label(title, systemImage: icon).font(.system(size: 11, weight: .medium))
        .padding(.horizontal, 9).frame(height: 34)
        .foregroundStyle(model.panels.contains(panel) ? Color.accentColor : Color.primary)
    }
    .help(title).accessibilityLabel(title)
  }
  private var statusBar: some View {
    HStack(spacing: 12) {
      Link(
        model.basemap.credit,
        destination: URL(
          string: model.basemap == .satellite
            ? "https://www.esri.com/" : "https://www.openstreetmap.org/copyright")!)
      Text(model.basemap == .satellite ? "Dated mosaic · not live" : "Community mapping")
        .foregroundStyle(.secondary)
      if model.terrainEnabled {
        Link(
          "Terrain credits",
          destination: URL(
            string: "https://github.com/tilezen/joerd/blob/master/docs/attribution.md")!)
      }
      if !model.sceneCredits.isEmpty {
        Text(model.sceneCredits).lineLimit(1).help(model.sceneCredits)
      }
      Spacer()
      Text(
        String(
          format: "%.4f°  %.4f°  ·  %.1f km", model.camera.center.latitude,
          model.camera.center.longitude, model.camera.altitude / 1000)
      ).monospacedDigit()
    }.font(.system(size: 10)).padding(.horizontal, 14).padding(.vertical, 8).background(.bar)
  }
  @ViewBuilder private func panelContent(_ panel: WorkspacePanel) -> some View {
    switch panel {
    case .sources: DataSourcesView(model: model)
    case .inspector: RecordInspectorView(model: model)
    case .satellite:
      SatellitePanel(
        model: model, satellite: model.satellite, sar: model.sar, optical: model.optical)
    case .mission:
      Button("Open mission window") { model.revealMission?() }
    case .raw: RawDataView(model: model)
    case .map: MapLayerView(model: model, missions: model.missions)
    case .tools: WorkspaceTools(model: model, onClose: { model.closePanel(.tools) })
    }
  }
}

/// In-window native tool palettes: map input outside the palette always remains available.
struct FloatingWorkspacePanel<Content: View>: View {
  var panel: WorkspacePanel
  var available: CGSize
  var close: () -> Void
  var focus: () -> Void
  var isFrontmost: Bool
  @ViewBuilder var content: () -> Content
  @State private var offset = CGSize.zero
  @State private var width: CGFloat = 0
  @State private var height: CGFloat = 580
  @State private var collapsed = false
  @GestureState private var drag = CGSize.zero
  @GestureState private var resize = CGSize.zero
  private var panelWidth: CGFloat {
    min(
      max(panel.minimumWidth, (width == 0 ? panel.width : width) + resize.width),
      available.width - 24)
  }
  private var panelHeight: CGFloat {
    collapsed ? 42 : min(max(280, height + resize.height), available.height - 150)
  }
  private var x: CGFloat {
    let start: CGFloat =
      [.mission, .sources, .inspector].contains(panel) ? 14 : available.width - panelWidth - 14
    return min(max(8, start + offset.width + drag.width), available.width - panelWidth - 8)
  }
  private var y: CGFloat {
    min(max(102, 108 + offset.height + drag.height), available.height - panelHeight - 34)
  }
  var body: some View {
    VStack(spacing: 0) {
      HStack(spacing: 9) {
        HStack(spacing: 9) {
          Image(systemName: panel.icon).foregroundStyle(.tint)
          Text(panel.title).font(.system(size: 13, weight: .semibold))
          Spacer()
        }.frame(height: 42).contentShape(Rectangle())
          .gesture(
            DragGesture(minimumDistance: 2, coordinateSpace: .global).updating($drag) {
              value, state, _ in state = value.translation
            }
            .onEnded { value in
              focus()
              offset.width += value.translation.width
              offset.height += value.translation.height
            })
        Menu {
          Button("Reset size and position") {
            offset = .zero
            width = 0
            height = 580
            collapsed = false
            focus()
          }
        } label: {
          Image(systemName: "ellipsis")
        }
        .menuStyle(.borderlessButton).fixedSize().help("Panel layout")
        Button {
          collapsed.toggle()
        } label: {
          Image(systemName: collapsed ? "plus" : "minus")
        }
        .help(collapsed ? "Expand panel" : "Collapse panel").accessibilityLabel(
          "Collapse or expand \(panel.title)")
        Button(action: close) { Image(systemName: "xmark") }
          .help("Close panel").accessibilityLabel("Close \(panel.title)")
      }.buttonStyle(.plain).padding(.horizontal, 14).frame(height: 42)
      if !collapsed {
        Divider()
        content().frame(maxWidth: .infinity, maxHeight: .infinity)
      }
    }.frame(width: panelWidth, height: panelHeight)
      .background(
        Color(nsColor: .windowBackgroundColor).opacity(0.98), in: RoundedRectangle(cornerRadius: 10)
      )
      .overlay(
        RoundedRectangle(cornerRadius: 10).strokeBorder(
          isFrontmost ? Color.accentColor.opacity(0.4) : WorkspaceDesign.border, lineWidth: 1)
      )
      .shadow(color: .black.opacity(0.2), radius: 14, y: 5)
      .overlay(alignment: .bottomTrailing) {
        if !collapsed {
          Image(systemName: "line.3.horizontal.decrease").font(.system(size: 9)).foregroundStyle(
            .secondary
          )
          .rotationEffect(.degrees(-45)).padding(6).contentShape(Rectangle())
          .gesture(
            DragGesture(minimumDistance: 2, coordinateSpace: .global).updating($resize) {
              value, state, _ in state = value.translation
            }
            .onEnded { value in
              width = min(
                max(
                  panel.minimumWidth, (width == 0 ? panel.width : width) + value.translation.width),
                available.width - 24)
              height = min(max(280, height + value.translation.height), available.height - 150)
            }
          ).help("Drag to resize panel")
        }
      }.simultaneousGesture(TapGesture().onEnded { focus() })
      .offset(x: x, y: y).accessibilityIdentifier("floating-\(panel.rawValue)")
  }
}

struct MapLayerView: View {
  @ObservedObject var model: WorkspaceModel
  @ObservedObject var missions: MissionWorkspace
  var body: some View {
    ScrollView {
      VStack(alignment: .leading, spacing: 14) {
        Picker("Basemap", selection: $model.basemap) {
          ForEach(Basemap.allCases, id: \.self) { Text($0.name).tag($0) }
        }
        Toggle("Terrain elevation", isOn: $model.terrainEnabled)
        Button("North-up overhead view") { model.orient(heading: 0, tilt: 0) }
        Divider()
        Text("Evidence layer").font(.headline)
        Picker("Display", selection: $model.rasterMode) {
          Text("Basemap only").tag("None")
          Text("Selected optical image").tag("Optical")
          Text("Selected SAR output").tag("SAR")
          Text("Selected change review").tag("Review")
        }
        Toggle("Mission comparison layers", isOn: $missions.comparisonEnabled)
        Text(
          "Select the acquisition in Satellite analysis, or choose a pair in Mission Control → Evidence."
        )
        .font(.caption).foregroundStyle(.secondary)
        Divider()
        Text("Active data sources").font(.headline)
        ForEach(FeedKind.allCases.filter { model.enabled.contains($0) }, id: \.self) { feed in
          Toggle(
            feed.name,
            isOn: Binding(
              get: { model.enabled.contains(feed) }, set: { model.setEnabled(feed, $0) }))
        }
        if model.enabled.isEmpty { Text("No data sources enabled.").foregroundStyle(.secondary) }
        Divider()
        Toggle("Solar terminator", isOn: $model.solarEnabled)
        Picker("Display style", selection: $model.displayStyle) {
          ForEach(DisplayStyle.allCases, id: \.self) { Text($0.name).tag($0) }
        }
        if model.displayStyle != .standard {
          Text("Visual shader effect; not a sensor.").font(.caption)
        }
        Button("Refresh sources") { model.refresh() }
      }.padding(14)
    }
  }
}
struct DataSourcesView: View {
  @ObservedObject var model: WorkspaceModel
  var body: some View {
    VStack(alignment: .leading, spacing: 12) {
      Text("Data sources").font(.title3.bold())
      TextField("Find a source", text: $model.sourceQuery).textFieldStyle(.roundedBorder)
        .accessibilityIdentifier("source-search")
      Toggle("Selected only", isOn: $model.selectedOnly)
      ScrollView {
        VStack(alignment: .leading, spacing: 14) {
          ForEach(model.sources.filter { model.sectionSources.contains($0) }, id: \.self) { feed in
            VStack(alignment: .leading, spacing: 5) {
              Toggle(
                feed.name,
                isOn: Binding(
                  get: { model.enabled.contains(feed) }, set: { model.setEnabled(feed, $0) })
              ).accessibilityIdentifier("source-\(feed.rawValue)")
              if model.enabled.contains(feed) {
                if feed == .satellites {
                  Picker("Catalog", selection: $model.sourceConfiguration.satelliteGroup) {
                    ForEach(SatelliteGroup.allCases, id: \.self) { Text($0.name).tag($0) }
                  }
                }
                if feed == .transit {
                  Picker("Operator", selection: $model.sourceConfiguration.transitID) {
                    ForEach(TransitOperator.catalog) { Text($0.name).tag($0.id) }
                  }
                  Button("View operator region") {
                    if let provider = TransitOperator.catalog.first(where: {
                      $0.id == model.sourceConfiguration.transitID
                    }) {
                      model.fly(to: provider.center, altitude: 80_000)
                    }
                  }
                }
                if feed == .cameras {
                  Picker("Region", selection: $model.sourceConfiguration.cameraProvider) {
                    ForEach(CameraProvider.allCases, id: \.self) { Text($0.name).tag($0) }
                  }
                  Button("View camera region") {
                    model.fly(to: model.sourceConfiguration.cameraProvider.center, altitude: 80_000)
                  }
                }
                if feed == .sentinel {
                  Picker("Collection", selection: $model.sourceConfiguration.sentinelKind) {
                    Text("Sentinel-2 optical").tag("SENTINEL-2")
                    Text("Sentinel-1 SAR metadata").tag("SENTINEL-1")
                  }
                }
                if model.loading.contains(feed) { ProgressView().controlSize(.small) }
                Text(
                  "\(model.snapshots[feed]?.records.count ?? 0) \((model.snapshots[feed]?.records.count ?? 0) == 1 ? "record" : "records") · \(model.snapshots[feed]?.source ?? "Awaiting source")"
                ).font(.caption).foregroundStyle(.secondary)
                if let rejected = model.snapshots[feed]?.rejected, rejected > 0 {
                  Text("Partial: \(rejected) unusable or duplicate records excluded").font(.caption)
                    .foregroundStyle(.orange)
                }
                ForEach(model.snapshots[feed]?.notices ?? [], id: \.self) { notice in
                  Text(notice).font(.caption2).foregroundStyle(.secondary).fixedSize(
                    horizontal: false, vertical: true)
                }
                if let error = model.errors[feed] {
                  Text(error).font(.caption).foregroundStyle(.orange)
                }
                if let time = model.fetched[feed] {
                  Text("Fetched \(time.formatted(date: .omitted, time: .standard))").font(.caption)
                    .foregroundStyle(.secondary)
                }
              }
              Text(feed.credit).font(.caption2).foregroundStyle(.secondary).fixedSize(
                horizontal: false, vertical: true)
            }
            Divider()
          }
          if model.sources.isEmpty { Text("No matching sources").foregroundStyle(.secondary) }
        }
      }
      Spacer(minLength: 0)
    }.padding(16)
  }
}
struct RecordInspectorView: View {
  @ObservedObject var model: WorkspaceModel
  var body: some View {
    VStack(alignment: .leading, spacing: 12) {
      Text("Inspect").font(.title3.bold())
      if let record = model.selected {
        ScrollView {
          VStack(alignment: .leading, spacing: 10) {
            Text(record.label).font(.headline).textSelection(.enabled)
            LabeledContent("Source", value: record.source)
            LabeledContent(
              "Source time",
              value: record.observedAt?.formatted(date: .abbreviated, time: .standard) ?? "Unknown")
            LabeledContent("Latitude", value: String(format: "%.5f°", record.coordinate.latitude))
            LabeledContent("Longitude", value: String(format: "%.5f°", record.coordinate.longitude))
            LabeledContent(
              "Geometric altitude",
              value: record.altitude.map { String(format: "%.0f m", $0) } ?? "Unknown")
            ForEach(
              record.details.keys.filter { !["Frame URL", "Video URL"].contains($0) }.sorted(),
              id: \.self
            ) { key in
              if key == "Source URL", let url = URL(string: record.details[key]!) {
                Link("Open source record", destination: url)
              } else {
                LabeledContent(key, value: record.details[key]!)
              }
            }
            if [.flights, .military, .vessels, .transit, .satellites].contains(record.feed) {
              Button(model.trackedID == record.id ? "Stop following" : "Follow target") {
                model.followSelection()
              }
              if record.feed == .satellites {
                Button("Show estimated orbit") { model.requestOrbit() }
                Text(
                  "One future orbital period, sampled from the catalog TLE. This is an estimate, not an observed trail."
                ).font(.caption).foregroundStyle(.secondary)
                if let date = model.orbitDate {
                  Text("Orbit estimate from " + date.formatted(date: .abbreviated, time: .standard))
                    .font(.caption).foregroundStyle(.secondary)
                }
                if let error = model.orbitError { Text(error).foregroundStyle(.orange) }
              } else {
                Toggle("Show retained trail", isOn: $model.showTrails)
                Text("\(model.retainedFixes.count) observed fixes retained during this session")
                  .font(.caption).foregroundStyle(.secondary)
                if model.retainedFixes.count > 1 {
                  Picker("Position time", selection: $model.playbackTime) {
                    Text("Current feed").tag(nil as Date?)
                    ForEach(model.retainedFixes, id: \.time) { fix in
                      Text(fix.time.formatted(date: .omitted, time: .standard)).tag(
                        fix.time as Date?)
                    }
                  }
                }
              }
            }
            if record.feed == .cameras {
              Button("Inspect camera media…") { model.cameraRecord = record }
            }
            HStack {
              Button("Link to mission") { model.addMissionRecord() }.disabled(
                model.missions.plan == nil)
              Button("Raw data") {
                model.rawScope = "Selected record"
                model.openPanel(.raw)
              }
            }
            Button("Center on record") { model.focusSelection() }
            Button("Clear selection") { model.selectedID = nil }
          }.font(.callout).textSelection(.enabled)
        }.frame(maxHeight: 310)
      } else if let point = model.surfaceSelection {
        Text("Picked surface").font(.headline)
        LabeledContent("Latitude", value: String(format: "%.6f°", point.coordinate.latitude))
        LabeledContent("Longitude", value: String(format: "%.6f°", point.coordinate.longitude))
        LabeledContent("Rendered height", value: String(format: "%.1f m", point.height))
        Text(model.surfaceEvidence).font(.caption).foregroundStyle(.secondary)
        Button("Clear surface selection") { model.surfaceSelection = nil }
      } else {
        Text(
          "Click the surface to inspect its loaded geometry, or enable a source and select a record."
        ).foregroundStyle(
          .secondary)
      }
      Divider()
      HStack {
        Text("Records").font(.headline)
        Spacer()
        Text("\(model.records.count)").foregroundStyle(.secondary)
      }
      TextField("Filter records", text: $model.recordQuery).textFieldStyle(.roundedBorder)
      List(selection: $model.selectedID) {
        ForEach(model.filteredRecords) { record in
          VStack(alignment: .leading, spacing: 3) {
            Text(record.label).lineLimit(2)
            Text(record.source).font(.caption).foregroundStyle(.secondary)
          }.tag(record.id)
        }
      }.listStyle(.inset)
    }.padding(16)
  }
}
struct AboutView: View {
  @Environment(\.dismiss) var dismiss
  var body: some View {
    VStack(alignment: .leading, spacing: 18) {
      HStack(spacing: 14) {
        Image(systemName: "globe.europe.africa.fill").font(.system(size: 36)).foregroundStyle(.tint)
        VStack(alignment: .leading, spacing: 4) {
          Text("Helios").font(.largeTitle.weight(.semibold))
          Text("Native geospatial analysis for macOS · Apple Silicon")
            .font(.callout).foregroundStyle(.secondary)
        }
      }
      Divider()
      Text("Map & sources").font(.headline)
      Text(
        "Explore terrain and 3D Tiles, inspect public aircraft and vessel reports, and connect infrastructure, environmental and satellite data with source attribution."
      )
      Text("Satellite analysis").font(.headline)
      Text(
        "Find dated optical imagery, process Sentinel-1 GRD radar data with SNAP, and review measured changes over saved sites. Source resolution, coverage and processing quality determine what can be inferred."
      )
      Text("Mission Control").font(.headline)
      Text(
        "Organize areas, evidence, notes, observations, tasks and acquisition checks in a separate window connected to the map. Mission documents and image libraries are stored on this Mac; provider credentials use macOS Keychain."
      )
      Text("Scientific limits").font(.headline)
      Text(
        "Radar changes and model suggestions are evidence for review. Independent radiometric/geographic validation and evaluation against documented events remain necessary. Camera clearance uses loaded map geometry and is not certified for real-world navigation."
      )
      .font(.callout).foregroundStyle(.secondary)
      HStack {
        if let notices = Bundle.main.url(forResource: "SCENE-NOTICES", withExtension: "txt") {
          Button("Component notices") {
            NSWorkspace.shared.open(notices)
          }
        }
        Spacer()
        Button("Close") { dismiss() }.keyboardShortcut(.defaultAction)
      }
    }.padding(24).frame(width: 580)
  }
}
