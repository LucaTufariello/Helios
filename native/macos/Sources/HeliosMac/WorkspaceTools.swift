import AVKit
import AppKit
import HeliosCore
import SwiftUI

struct WorkspaceTools: View {
  @ObservedObject var model: WorkspaceModel
  var onClose: () -> Void = {}
  @State private var tilesetAddress = ""
  @State private var noteTitle = ""
  @State private var firmsKey = ""
  @State private var aisKey = ""
  @State private var tomtomKey = ""
  @State private var providerMessage: String?
  var body: some View {
    VStack(alignment: .leading, spacing: 16) {
      HStack {
        Text(model.toolTab).font(.headline)
        Spacer()
        Button("Done") {
          model.showTools = false
          onClose()
        }.keyboardShortcut(.cancelAction)
      }
      Picker("Tool", selection: $model.toolTab) {
        ForEach(
          ["3D Surface", "Directions", "Annotations", "Providers", "Workspace"], id: \.self
        ) {
          Text($0).tag($0)
        }
      }.pickerStyle(.segmented)
      ScrollView {
        VStack(alignment: .leading, spacing: 16) {
          switch model.toolTab {
          case "3D Surface": surfaceTools
          case "Directions": directions
          case "Annotations": annotationTools
          case "Providers": providerTools
          default: workspaceTools
          }
          if let error = model.storageError { Text(error).foregroundStyle(.orange) }
        }.frame(maxWidth: .infinity, alignment: .leading)
      }
    }.padding(14).frame(maxWidth: .infinity, maxHeight: .infinity)
  }
  private var terrainCredits: String {
    guard
      let url = Bundle.module.url(
        forResource: "TERRAIN-ATTRIBUTION", withExtension: "md", subdirectory: "Resources"),
      let text = try? String(contentsOf: url, encoding: .utf8),
      let start = text.range(of: "***Required attribution:***"),
      let end = text.range(
        of: "***Required attribution when using Mapzen's hosted service:***",
        range: start.upperBound..<text.endIndex)
    else {
      return
        "Terrain sources: ArcticDEM, Geoscience Australia, Austria, Canada, Copernicus/EU, NOAA, INEGI, LINZ, Kartverket, Environment Agency, USGS. See the attribution link for full notices."
    }
    return String(text[start.upperBound..<end.lowerBound]).replacingOccurrences(of: "```", with: "")
      .trimmingCharacters(in: .whitespacesAndNewlines)
  }
  private var surfaceTools: some View {
    VStack(alignment: .leading, spacing: 12) {
      Text("Terrain and camera").font(.headline)
      Toggle("Show terrain elevation", isOn: $model.terrainEnabled)
      Text(model.terrainStatus).font(.callout).foregroundStyle(.secondary)
      HStack {
        Text("Tilt")
        Slider(
          value: Binding(
            get: { model.camera.tilt },
            set: { model.orient(heading: model.camera.heading, tilt: $0) }), in: 0...80)
        Text(String(format: "%.0f°", model.camera.tilt)).monospacedDigit().frame(width: 40)
      }
      HStack {
        Text("Heading")
        Slider(
          value: Binding(
            get: { model.camera.heading },
            set: { model.orient(heading: $0, tilt: model.camera.tilt) }), in: 0...359)
        Text(String(format: "%.0f°", model.camera.heading)).monospacedDigit().frame(width: 40)
        Button("North up") { model.orient(heading: 0, tilt: model.camera.tilt) }
      }
      Text(
        "Option-drag to tilt and rotate. Drag to pan; scroll to zoom. Camera distance is measured from the loaded surface at the view center."
      ).font(.caption).foregroundStyle(.secondary)
      Link(
        "Mapzen terrain data and attribution",
        destination: URL(
          string: "https://github.com/tilezen/joerd/blob/master/docs/attribution.md")!)
      Text(
        "Source elevations use mixed local vertical datums. Clearance applies to loaded geometry; unavailable areas use a smooth ellipsoid."
      ).font(.caption).foregroundStyle(.secondary)
      DisclosureGroup("Terrain source credits") {
        Text(terrainCredits).font(.caption).textSelection(.enabled)
      }
      Divider()
      Text("Photorealistic 3D Tiles").font(.headline)
      TextField("HTTPS tileset.json URL", text: $tilesetAddress).textFieldStyle(.roundedBorder)
      HStack {
        Button("Load URL") {
          if let url = URL(string: tilesetAddress.trimmingCharacters(in: .whitespacesAndNewlines)) {
            model.loadTileset(url)
          } else {
            model.surfaceError = "Enter a valid HTTPS tileset URL."
          }
        }.disabled(tilesetAddress.isEmpty)
        Button("Open local tileset…") { model.openTileset() }
        Button("Focus coverage") {
          model.focusTileset()
          model.showTools = false
          onClose()
        }.disabled(model.tilesetURL == nil)
        Button("Remove") { model.clearTileset() }.disabled(model.tilesetURL == nil)
      }
      Button("Load public AGI headquarters photogrammetry") {
        model.loadTileset(
          URL(string: "https://pelican-public.s3.amazonaws.com/3dtiles/agi-hq/tileset.json")!)
      }
      Text(model.sceneStatus).font(.callout)
      if let url = model.tilesetURL {
        Text(url.isFileURL ? url.lastPathComponent : url.host ?? "HTTPS tileset").font(.caption)
          .foregroundStyle(.secondary)
      }
      if !model.sceneCredits.isEmpty { Text(model.sceneCredits).font(.caption) }
      if let error = model.surfaceError { Text(error).foregroundStyle(.orange) }
      Text(
        "Public AGI coverage is actual captured geometry from the Cesium 3D Tiles resources. Loading a tileset does not grant rights to redistribute it. Coverage and acquisition dates depend on its publisher. The selected source is kept only for this session."
      ).font(.caption).foregroundStyle(.secondary)
      Link(
        "Public 3D Tiles datasets",
        destination: URL(string: "https://github.com/CesiumGS/3d-tiles/blob/main/RESOURCES.md")!)
      if let point = model.surfaceSelection {
        Divider()
        Text("Picked surface").font(.headline)
        Text(
          String(
            format: "%.6f°, %.6f° · %.1f m", point.coordinate.latitude, point.coordinate.longitude,
            point.height)
        ).monospacedDigit()
        Text(model.surfaceEvidence).font(.caption).foregroundStyle(.secondary)
      }
    }
  }
  private var directions: some View {
    VStack(alignment: .leading, spacing: 12) {
      Text("Directions").font(.headline)
      Picker("Travel", selection: $model.travelMode) {
        ForEach(TravelMode.allCases, id: \.self) { Text($0.name).tag($0) }
      }
      ForEach(["A", "B"], id: \.self) { endpoint in
        HStack {
          Text("\(endpoint): \(coordinate(endpoint == "A" ? model.routeA : model.routeB))").font(
            .callout.monospacedDigit())
          Spacer()
          Button("Use view center") {
            if endpoint == "A" {
              model.routeA = model.camera.center
            } else {
              model.routeB = model.camera.center
            }
          }
          Button("Pick on map") {
            model.routeEndpoint = endpoint
            model.showTools = false
            onClose()
          }
        }
      }
      HStack {
        Button("Find route") { model.requestRoute() }.disabled(
          model.routeA == nil || model.routeB == nil || model.routeLoading)
        Button("Clear") { model.clearRoute() }
        Button("Fly route") {
          model.flyRoute()
          model.showTools = false
          onClose()
        }.disabled(model.route == nil)
      }
      if model.routeLoading { ProgressView("Requesting route…") }
      if let error = model.routeError { Text(error).foregroundStyle(.orange) }
      if let route = model.route {
        Text(String(format: "%.1f km · %.0f min", route.distance / 1000, route.duration / 60)).font(
          .headline)
        ForEach(route.steps) { step in
          Button {
            model.fly(to: step.coordinate, altitude: 10_000)
            model.showTools = false
            onClose()
          } label: {
            HStack {
              Text(step.instruction)
              Spacer()
              Text(String(format: "%.0f m", step.distance)).foregroundStyle(.secondary)
            }
          }.buttonStyle(.plain).padding(.vertical, 4)
        }
      }
      Text(
        "Routing: routing.openstreetmap.de / OSRM · © OpenStreetMap contributors. Route lines drape over loaded elevation. Missing elevation uses the ellipsoid; source heights are approximate."
      ).font(.caption).foregroundStyle(.secondary)
    }
  }
  private var annotationTools: some View {
    VStack(alignment: .leading, spacing: 12) {
      TextField("Annotation title", text: $noteTitle).textFieldStyle(.roundedBorder)
      Button("Add note at view center") {
        model.addAnnotationAtCenter(noteTitle)
        noteTitle = ""
      }.disabled(noteTitle.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty)
      Divider()
      TextField("Drawing title", text: $model.drawingTitle).textFieldStyle(.roundedBorder)
      Toggle("Closed polygon", isOn: $model.closeDrawing)
      Text("\(model.drawingPoints.count) vertices").foregroundStyle(.secondary)
      HStack {
        Button("Draw on map") {
          model.drawing = true
          model.showTools = false
          onClose()
        }.disabled(model.drawingTitle.isEmpty)
        Button("Add view center") { model.drawingPoints.append(model.camera.center) }.disabled(
          model.drawingPoints.count >= 2000)
        Button("Save drawing") { model.commitDrawing() }.disabled(
          model.drawingPoints.isEmpty || model.drawingTitle.isEmpty)
        Button("Cancel drawing") {
          model.drawing = false
          model.drawingPoints = []
        }
      }
      Text(
        "Saved on this Mac. Notes and drawings are user-authored; they do not imply a provider observation."
      ).font(.caption).foregroundStyle(.secondary)
      Divider()
      ForEach(model.annotations) { note in
        HStack {
          Button(note.title) {
            if let point = note.points.first {
              model.fly(to: point, altitude: 20_000)
              model.showTools = false
              onClose()
            }
          }.buttonStyle(.plain)
          Spacer()
          Button("Delete") { model.saveAnnotations(model.annotations.filter { $0.id != note.id }) }
        }
      }
    }
  }
  private var providerTools: some View {
    VStack(alignment: .leading, spacing: 12) {
      Text("Provider credentials").font(.headline)
      Text(
        "Keys are stored in macOS Keychain and sent only to the corresponding provider. Workspace files and view links contain no keys."
      ).font(.callout).foregroundStyle(.secondary)
      SecureField("NASA FIRMS MAP_KEY", text: $firmsKey).textFieldStyle(.roundedBorder)
      HStack {
        Button("Save FIRMS key") {
          saveKey(firmsKey, name: "firms")
          firmsKey = ""
        }.disabled(firmsKey.isEmpty)
        Button("Remove FIRMS key") { saveKey("", name: "firms") }
        Link(
          "Get a free key",
          destination: URL(string: "https://firms.modaps.eosdis.nasa.gov/api/map_key/")!)
      }
      SecureField("AISStream API key", text: $aisKey).textFieldStyle(.roundedBorder)
      HStack {
        Button("Save AIS key") {
          saveKey(aisKey, name: "ais")
          aisKey = ""
        }.disabled(aisKey.isEmpty)
        Button("Remove AIS key") { saveKey("", name: "ais") }
        Link("AISStream", destination: URL(string: "https://aisstream.io/")!)
      }
      SecureField("TomTom API key", text: $tomtomKey).textFieldStyle(.roundedBorder)
      HStack {
        Button("Save TomTom key") {
          saveKey(tomtomKey, name: "tomtom")
          tomtomKey = ""
        }.disabled(tomtomKey.isEmpty)
        Button("Remove TomTom key") { saveKey("", name: "tomtom") }
        Link("TomTom developer portal", destination: URL(string: "https://developer.tomtom.com/")!)
      }
      Text(
        "Traffic flow uses a local view, at most nine tiles, and a 1,000-request daily cap per app session. Your provider quota and terms still apply."
      ).font(.caption).foregroundStyle(.secondary)
      if let providerMessage { Text(providerMessage).font(.caption) }
      Text(
        "The remaining public sources need no credentials. Google 3D and Bing/Cesium ion need additional native integrations before their settings can be used here."
      ).font(.caption).foregroundStyle(.secondary)
    }
  }
  private var workspaceTools: some View {
    VStack(alignment: .leading, spacing: 12) {
      Text("Workspace and sharing").font(.headline)
      HStack {
        Button("Export workspace…") { model.exportWorkspace() }
        Button("Import workspace…") { model.importWorkspace() }
        Button("Copy view link") { model.copyLink() }
      }
      Text(
        "Workspace JSON includes annotations, view position and display choices. View links contain only view choices and enabled sources. Imported files are validated before replacing the current workspace."
      ).foregroundStyle(.secondary)
      Text("Bundled data licenses").font(.headline)
      Button("Open native scene component notices") {
        if let url = Bundle.main.url(forResource: "SCENE-NOTICES", withExtension: "txt") {
          NSWorkspace.shared.open(url)
        }
      }
      Text(
        "Datacenters and dams: OpenStreetMap / ODbL. Submarine cables: © TeleGeography / CC BY-NC-SA 3.0, for non-commercial use. These datasets keep their own licenses; the source-code MIT license does not replace them."
      ).font(.callout)
    }
  }
  private func coordinate(_ value: Coordinate?) -> String {
    value.map { String(format: "%.5f°, %.5f°", $0.latitude, $0.longitude) } ?? "Not placed"
  }
  private func saveKey(_ value: String, name: String) {
    do {
      try ProviderKeys.save(value, name: name)
      if name == "firms" {
        model.sourceConfiguration.firmsKey = value.trimmingCharacters(in: .whitespacesAndNewlines)
      }
      if name == "ais", model.enabled.contains(.vessels) {
        model.setEnabled(.vessels, false)
        model.setEnabled(.vessels, true)
      }
      if name == "tomtom", model.enabled.contains(.traffic) {
        model.setEnabled(.traffic, false)
        model.setEnabled(.traffic, true)
      }
      providerMessage = value.isEmpty ? "Credential removed." : "Credential saved in Keychain."
    } catch { providerMessage = error.localizedDescription }
  }
}
struct CameraViewer: View {
  let record: GeoRecord
  @Environment(\.dismiss) private var dismiss
  @State private var image: NSImage?
  @State private var error: String?
  @State private var fetchedAt: Date?
  @State private var loading = false
  @State private var player: AVPlayer?
  @State private var task: Task<Void, Never>?
  @State private var loadGeneration = UUID()
  private let transport = Transport()
  var body: some View {
    VStack(alignment: .leading, spacing: 12) {
      HStack {
        Text(record.label).font(.title3.bold())
        Spacer()
        Button("Close") { dismiss() }
      }
      if let player {
        NativeCameraPlayer(player: player).frame(height: 340)
      } else if let image {
        Image(nsImage: image).resizable().scaledToFit().frame(maxHeight: 340)
      } else {
        ContentUnavailableView(
          "No frame loaded", systemImage: "camera",
          description: Text("Load an actual operator frame to inspect it."))
      }
      if loading { ProgressView("Loading operator frame…") }
      if let error { Text(error).foregroundStyle(.orange) }
      Text("\(record.source) · frame acquisition time unknown; images and video may be delayed.")
        .font(.caption)
      if let fetchedAt { Text("Still frame retrieved \(fetchedAt.formatted())").font(.caption) }
      HStack {
        Button("Load still frame") { load() }.disabled(loading)
        if let raw = record.details["Video URL"], let url = CameraProvider.admit(raw) {
          Button("Play operator clip") {
            task?.cancel()
            loadGeneration = UUID()
            loading = false
            fetchedAt = nil
            player?.pause()
            player = AVPlayer(url: url)
            player?.play()
          }
        }
        Button("Stop") {
          player?.pause()
          player = nil
          task?.cancel()
          loadGeneration = UUID()
          loading = false
        }
      }
    }.padding(20).frame(width: 680, height: 520)
      .onDisappear {
        task?.cancel()
        player?.pause()
        transport.session.invalidateAndCancel()
      }
  }
  private func load() {
    guard let raw = record.details["Frame URL"], let url = CameraProvider.admit(raw) else {
      error = "No supported frame URL."
      return
    }
    task?.cancel()
    player?.pause()
    player = nil
    loading = true
    error = nil
    let generation = UUID()
    loadGeneration = generation
    task = Task {
      defer { if loadGeneration == generation { loading = false } }
      do {
        let data = try await transport.data(
          url, maximumBytes: 4 * 1024 * 1024, allowRedirects: false)
        try Task.checkCancellation()
        guard loadGeneration == generation else { return }
        guard let image = NSImage(data: data) else { throw FeedError.malformed }
        self.image = image
        fetchedAt = Date()
      } catch is CancellationError {} catch {
        if loadGeneration == generation { self.error = error.localizedDescription }
      }
    }
  }
}

// Use AppKit's AVPlayerView directly. The SwiftUI VideoPlayer overlay crashes in
// superclass metadata initialization on the macOS 27 runtime used for validation.
struct NativeCameraPlayer: NSViewRepresentable {
  let player: AVPlayer
  func makeNSView(context: Context) -> AVPlayerView {
    let view = AVPlayerView()
    view.controlsStyle = .inline
    view.videoGravity = .resizeAspect
    view.showsFullScreenToggleButton = false
    view.player = player
    return view
  }
  func updateNSView(_ view: AVPlayerView, context: Context) {
    if view.player !== player {
      view.player?.pause()
      view.player = player
    }
  }
  static func dismantleNSView(_ view: AVPlayerView, coordinator: ()) {
    view.player?.pause()
    view.player = nil
  }
}
