import AppKit
import Darwin
import HeliosCore
import SwiftUI

struct SavedOpticalImage: Identifiable, Sendable {
  let id: UUID
  let directory: URL
  let result: SentinelOpticalResult
  var title: String {
    let date = String((result.scene?.acquiredAt ?? "Unknown date").prefix(16))
      .replacingOccurrences(of: "T", with: " ")
    guard let region = result.region else { return date }
    let center = String(
      format: "%.4f, %.4f", (region.south + region.north) / 2,
      (region.west + region.east) / 2)
    let view =
      result.mode == "spectral"
      ? result.wavelengthsNanometres?.first.map { String(format: "%.1f nm", $0) } ?? "Spectral"
      : (result.mode ?? "RGB").uppercased()
    return
      "\(result.scene?.sensor.shortName ?? "Optical") · \(view) · \(date) UTC · \(center) · \(result.width ?? 0) × \(result.height ?? 0)"
  }
}

@MainActor final class OpticalWorkspace: ObservableObject {
  @Published var source: SatelliteSource = .sentinel2
  @Published var mode = "pan"
  @Published var wavelengthNanometres = 660.0
  @Published var localProduct: URL?
  @Published var clipPrismaToRegion = true
  var visibleImages: [SavedOpticalImage] { images.filter { $0.result.scene?.sensor == source } }
  var sourceDescription: String {
    switch source {
    case .sentinel2:
      "Public dated L2A true colour, 10 m RGB pixels. No sign-in required. This cannot resolve small buildings or vehicles."
    case .cbers4a:
      "Public orthorectified WPM archive: 2 m grayscale PAN or native 8 m RGB. Coverage is regional, predominantly South America; check the selected region for available acquisitions. Cloud metadata may be unknown."
    case .prisma:
      "Import geocoded L2D HE5 from ASI. Free registration and product ordering are required. PAN is 5 m grayscale; RGB and spectral bands are 30 m. This viewer does not identify materials automatically."
    case .sentinel1: "Calibrated radar backscatter through the existing SAR workflow."
    }
  }
  @Published var startDate = Calendar.current.date(byAdding: .day, value: -30, to: Date())!
  @Published var endDate = Date()
  @Published var maximumCloud = 20.0
  @Published var scenes: [SentinelOpticalScene] = []
  @Published var sceneID: String?
  @Published var query: SentinelOpticalQuery?
  @Published var searching = false
  @Published var state = SarState.completed
  @Published var fraction: Double?
  @Published var status = "Choose a region and search for dated Sentinel-2 L2A images."
  @Published var error: String?
  @Published private(set) var loadDirectory: URL?
  @Published var images: [SavedOpticalImage] = []
  @Published var imageID: UUID?
  @Published var showOverlay = true
  @Published var pixelGrid = true
  @Published var exporting = false
  @Published var deleting = false
  private var searchTask: Task<Void, Never>?
  private var loadTask: Task<Void, Never>?
  private var searchGeneration = UUID()
  private var loadGeneration = UUID()
  private var reloadGeneration = UUID()
  var busy: Bool { state.isActive }
  var scene: SentinelOpticalScene? { scenes.first { $0.id == sceneID } }
  var image: SavedOpticalImage? { images.first { $0.id == imageID } }
  static var storage: URL {
    SatelliteWorkspace.storage.appendingPathComponent("optical", isDirectory: true)
  }
  var overlay: SarOverlay? {
    guard showOverlay, let image, let scene = image.result.scene, let bounds = image.result.bounds,
      scene.sensor == source
    else { return nil }
    return SarOverlay(
      preview: image.directory.appendingPathComponent("preview.png"), bounds: bounds,
      label: "\(scene.platform) · \(image.result.mode ?? "true colour") · \(scene.acquiredAt)",
      pixelGrid: pixelGrid, opacity: 1)
  }
  init() { reload() }
  func resetSearch() {
    searchTask?.cancel()
    searchGeneration = UUID()
    searching = false
    scenes = []
    sceneID = nil
    query = nil
  }
  func search(region: SentinelRegion?) {
    guard !searching, !busy else { return }
    let query: SentinelOpticalQuery
    do {
      guard let region else { throw SentinelOpticalError.query }
      query = try SentinelOpticalQuery(
        region: region, start: startDate, end: endDate, maximumCloud: maximumCloud, source: source)
    } catch {
      self.error = error.localizedDescription
      return
    }
    resetSearch()
    self.query = query
    error = nil
    searching = true
    let generation = searchGeneration
    status =
      source == .cbers4a
      ? "Searching INPE CBERS-4A WPM L4 catalogue…"
      : "Searching the public Earth Search Sentinel-2 catalogue…"
    searchTask = Task {
      defer { if searchGeneration == generation { searching = false } }
      do {
        let scenes = try await SentinelOpticalCatalog().search(query)
        try Task.checkCancellation()
        guard searchGeneration == generation else { return }
        self.scenes = scenes
        sceneID = scenes.first?.id
        status =
          scenes.isEmpty
          ? (query.source == .cbers4a
            ? "No CBERS WPM L4 acquisitions matched. This regional archive is not global; changing resolution cannot create missing coverage."
            : "No supported images matched. Try a wider date range or higher cloud limit.")
          : "\(scenes.count) images found (up to 40 newest matches). Select a date to load its optical image."
      } catch {
        guard searchGeneration == generation, !Task.isCancelled else { return }
        self.error = error.localizedDescription
        status = "Optical catalogue search failed"
      }
    }
  }
  func load(
    python: String, region: SentinelRegion? = nil, onReady: @escaping @MainActor ([Double]) -> Void
  ) {
    guard !busy, !deleting, source != .sentinel1,
      source == .prisma ? localProduct != nil : scene != nil && query != nil,
      let worker = Bundle.module.url(
        forResource: "sentinel2", withExtension: "py", subdirectory: "Resources/Optical")
    else { return }
    let selectedSource = source
    let selectedScene = scene
    let selectedRegion = source == .prisma ? (clipPrismaToRegion ? region : nil) : query?.region
    let product = localProduct
    let displayMode = mode
    let wavelength = wavelengthNanometres
    let id = UUID()
    let directory = Self.storage.appendingPathComponent(id.uuidString, isDirectory: true)
    let request = Self.storage.appendingPathComponent(".request-\(id.uuidString).json")
    loadGeneration = id
    loadDirectory = directory
    reloadGeneration = UUID()
    error = nil
    state = .queued
    fraction = nil
    status = "Queued for optical image loading"
    loadTask = Task { [self] in
      defer { try? FileManager.default.removeItem(at: request) }
      do {
        if selectedSource == .prisma {
          try SatelliteDocuments.save(
            LocalOpticalRequest(
              source: .prisma, originalInput: product!.path,
              region: selectedRegion, mode: displayMode, wavelengthNanometres: wavelength),
            to: request)
        } else {
          var data = SentinelOpticalRequest(scene: selectedScene!, region: selectedRegion!)
          data.mode = selectedSource == .cbers4a ? displayMode : nil
          try SatelliteDocuments.save(data, to: request)
        }
        let processor = SarProcessor(python: URL(fileURLWithPath: python), worker: worker)
        let result = try await processor.renderOptical(request: request, destination: directory) {
          update in
          Task { @MainActor [weak self] in
            guard let self, self.loadGeneration == id, self.state.allows(update.state) else {
              return
            }
            self.state = update.state
            self.fraction = update.progress
            self.status = update.stage
          }
        }
        guard loadGeneration == id else { return }
        images.insert(SavedOpticalImage(id: id, directory: directory, result: result), at: 0)
        imageID = id
        showOverlay = true
        state = .completed
        fraction = 1
        status = "\(result.scene?.platform ?? "Optical") image ready."
        onReady(result.bounds!)
      } catch {
        guard loadGeneration == id else { return }
        state = .failed
        fraction = nil
        self.error =
          error is CancellationError ? "Optical request cancelled." : error.localizedDescription
        status = "Optical image loading failed"
      }
    }
  }
  func cancelLoad() { loadTask?.cancel() }
  func cancelAll() {
    resetSearch()
    cancelLoad()
  }
  func reload() {
    let root = Self.storage
    let generation = UUID()
    reloadGeneration = generation
    Task {
      let loaded = await Task.detached(priority: .utility) {
        let directories =
          (try? FileManager.default.contentsOfDirectory(
            at: root, includingPropertiesForKeys: nil)) ?? []
        return directories.prefix(500).compactMap { directory -> SavedOpticalImage? in
          guard let id = UUID(uuidString: directory.lastPathComponent),
            var result = try? SarProcessor.read(
              SentinelOpticalResult.self, from: directory.appendingPathComponent("result.json")),
            result.isValid
          else { return nil }
          if result.state.isActive, let pid = result.workerPID, pid > 0,
            kill(pid, 0) != 0 && errno == ESRCH
          {
            for name in [
              "preview.png", "true-color.tif", "source-values.tif", "selected.tmp.tif",
              "result.tmp",
            ] {
              try? FileManager.default.removeItem(at: directory.appendingPathComponent(name))
            }
            result.state = .failed
            result.error = "Interrupted optical request. Partial images were removed."
            try? SatelliteDocuments.save(
              result, to: directory.appendingPathComponent("result.json"))
          }
          guard result.state == .completed,
            FileManager.default.fileExists(
              atPath: directory.appendingPathComponent("preview.png").path),
            FileManager.default.fileExists(
              atPath: directory.appendingPathComponent("true-color.tif").path)
          else { return nil }
          return SavedOpticalImage(id: id, directory: directory, result: result)
        }.sorted { $0.result.createdAt > $1.result.createdAt }
      }.value
      guard reloadGeneration == generation else { return }
      images = loaded
      if imageID == nil { imageID = images.first?.id }
    }
  }
  func deleteImage(_ id: UUID) {
    guard !busy, !exporting, !deleting, let selected = images.first(where: { $0.id == id }) else {
      return
    }
    let alert = NSAlert()
    alert.messageText = "Move saved optical image to Trash?"
    alert.informativeText =
      "\(selected.title)\n\nThe saved preview, GeoTIFFs and processing metadata will be moved to the macOS Trash. Imported source products and exported copies will be kept."
    alert.addButton(withTitle: "Move to Trash")
    alert.addButton(withTitle: "Cancel")
    guard alert.runModal() == .alertFirstButtonReturn else { return }
    deleting = true
    reloadGeneration = UUID()
    let root = Self.storage
    Task {
      defer { deleting = false }
      do {
        try await Task.detached(priority: .utility) {
          try SatelliteDocuments.trashOpticalImage(id: id, root: root)
        }.value
        images.removeAll { $0.id == id }
        if imageID == id { imageID = visibleImages.first?.id }
        error = nil
        status = "Saved optical image moved to Trash."
      } catch {
        self.error = "The optical image could not be moved to Trash. It is still in the library."
      }
    }
  }
  func selectSource() {
    resetSearch()
    if image?.result.scene?.sensor != source { imageID = visibleImages.first?.id }
    error = nil
    mode = "pan"
    status =
      source == .prisma
      ? "Choose an extracted PRISMA L2D product."
      : "Choose a region and search the selected catalogue."
  }
  func importPrisma(region: SentinelRegion?) {
    guard !busy else { return }
    let panel = NSOpenPanel()
    panel.canChooseDirectories = false
    panel.allowsMultipleSelection = false
    panel.allowedContentTypes = [.init(filenameExtension: "he5") ?? .data]
    panel.message = "Select an extracted PRISMA L2D .he5 product."
    panel.directoryURL =
      localProduct?.deletingLastPathComponent()
      ?? FileManager.default.urls(for: .downloadsDirectory, in: .userDomainMask).first
    guard panel.runModal() == .OK, let url = panel.url else { return }
    localProduct = url
    if let region {
      query = try? SentinelOpticalQuery(
        region: region, start: startDate, end: endDate,
        maximumCloud: maximumCloud, source: .prisma)
    }
    error = nil
    status = "PRISMA product selected; projection, bands and quality masks will be checked on load."
  }
  func reveal() {
    NSWorkspace.shared.activateFileViewerSelecting([image?.directory ?? Self.storage])
  }
  func export(_ name: String) {
    guard let image,
      ["preview.png", "true-color.tif", "source-values.tif", "result.json"].contains(name),
      !exporting, !deleting
    else { return }
    let panel = NSSavePanel()
    panel.nameFieldStringValue = "\(image.result.scene?.id ?? "Sentinel-2")-\(name)"
    guard panel.runModal() == .OK, let target = panel.url else { return }
    let source = image.directory.appendingPathComponent(name)
    if name == "result.json" {
      do {
        var metadata = image.result
        metadata.workerPID = nil
        try SatelliteDocuments.save(metadata, to: target)
      } catch { self.error = "The optical metadata could not be saved." }
      return
    }
    exporting = true
    Task {
      defer { exporting = false }
      do {
        try await Task.detached(priority: .utility) {
          let temporary = target.deletingLastPathComponent().appendingPathComponent(
            ".helios-\(UUID().uuidString).tmp")
          defer { try? FileManager.default.removeItem(at: temporary) }
          try FileManager.default.copyItem(at: source, to: temporary)
          if FileManager.default.fileExists(atPath: target.path) {
            _ = try FileManager.default.replaceItemAt(target, withItemAt: temporary)
          } else {
            try FileManager.default.moveItem(at: temporary, to: target)
          }
        }.value
      } catch { self.error = "The optical export could not be saved." }
    }
  }
}

private struct LocalOpticalRequest: Encodable {
  let source: SatelliteSource
  let originalInput: String
  let region: SentinelRegion?
  let mode: String
  let wavelengthNanometres: Double
}

struct SentinelOpticalView: View {
  @ObservedObject var model: WorkspaceModel
  @ObservedObject var optical: OpticalWorkspace
  @ObservedObject var sar: SarWorkspace
  var body: some View {
    VStack(alignment: .leading, spacing: 12) {
      Text(optical.source.name).font(.headline)
      Text(optical.sourceDescription).font(.caption).foregroundStyle(.secondary)
      Link(
        optical.source == .prisma
          ? "ASI PRISMA registration and product access"
          : optical.source == .cbers4a
            ? "INPE / CBERS public archive" : "Copernicus Sentinel-2 · Earth Search / Element 84",
        destination: URL(
          string: optical.source == .prisma
            ? "https://www.asi.it/en/earth-science/prisma/"
            : optical.source == .cbers4a
              ? "https://registry.opendata.aws/cbers/"
              : "https://registry.opendata.aws/sentinel-2-l2a-cogs/")!
      ).font(.caption)
      VStack(alignment: .leading, spacing: 8) {
        HStack {
          Button("Draw optical region") { model.beginSarRegionSelection() }
          Button("Use current view") { model.setSarRegionToView() }
        }
        if let region = model.sarSearchRegion {
          Text(region.summary).font(.caption.monospacedDigit())
        } else {
          Text("Zoom to the area you want, then draw a region or use the current view.").font(
            .caption)
        }
        if optical.source == .cbers4a || optical.source == .prisma {
          Picker("Display product", selection: $optical.mode) {
            Text("Panchromatic grayscale").tag("pan")
            Text("Native RGB (no pansharpening)").tag("rgb")
            if optical.source == .prisma { Text("Selected spectral band").tag("spectral") }
          }
        }
        if optical.source == .prisma {
          Toggle("Clip PRISMA to selected region", isOn: $optical.clipPrismaToRegion)
          if optical.mode == "spectral" {
            TextField("Wavelength (nm)", value: $optical.wavelengthNanometres, format: .number)
            Text(
              "Nearest valid VNIR/SWIR band, 400–2500 nm. Actual wavelength is saved in metadata."
            ).font(.caption)
          }
          Button("Import PRISMA L2D HE5…") { optical.importPrisma(region: model.sarSearchRegion) }
          if let file = optical.localProduct {
            Text(file.lastPathComponent).font(.caption).textSelection(.enabled)
            Text(
              "Region: \(optical.clipPrismaToRegion ? model.sarSearchRegion?.summary ?? "whole product" : "whole product")"
            ).font(.caption)
            Button("Load PRISMA product") {
              optical.load(python: sar.pythonPath, region: model.sarSearchRegion) { bounds in
                model.dailyImagery = .off
                model.displayStyle = .standard
                model.orient(heading: 0, tilt: 0)
                model.focusSatelliteBounds(bounds)
              }
            }.buttonStyle(.borderedProminent)
          }
        } else {
          HStack {
            DatePicker("From (UTC)", selection: $optical.startDate, displayedComponents: .date)
            DatePicker("To (UTC)", selection: $optical.endDate, displayedComponents: .date)
          }.environment(\.timeZone, TimeZone(secondsFromGMT: 0)!)
          if optical.source == .sentinel2 {
            HStack {
              Text("Maximum tile cloud (%)")
              TextField("Cloud %", value: $optical.maximumCloud, format: .number).frame(width: 65)
            }
          }
          Button(optical.source == .cbers4a ? "Search CBERS-4A images" : "Search Sentinel-2 images")
          { optical.search(region: model.sarSearchRegion) }
          .buttonStyle(.borderedProminent).disabled(model.sarSearchRegion == nil)
        }
      }.disabled(optical.searching || optical.busy)
      if optical.searching {
        ProgressView()
        Button("Cancel search") {
          optical.resetSearch()
          optical.status = "Search cancelled."
        }
      }
      Text(optical.status).font(.callout)
      if let error = optical.error {
        Text(error).font(.caption).foregroundStyle(.orange).textSelection(.enabled)
        if let directory = optical.loadDirectory,
          FileManager.default.fileExists(
            atPath: directory.appendingPathComponent("diagnostics.log").path)
        {
          Button("Show diagnostic log in Finder") {
            NSWorkspace.shared.activateFileViewerSelecting([
              directory.appendingPathComponent("diagnostics.log")
            ])
          }
        }
      }
      if !optical.scenes.isEmpty {
        Picker("Acquisition", selection: $optical.sceneID) {
          ForEach(optical.scenes) { scene in
            Text(
              "\(scene.acquiredAt) · \(scene.platform) · \(scene.cloudCover.map { String(format: "%.1f%% cloud", $0) } ?? "cloud unknown")"
            ).tag(Optional(scene.id))
          }
        }.disabled(optical.busy)
        if let scene = optical.scene { Text(scene.id).font(.caption).textSelection(.enabled) }
        if let query = optical.query {
          Text("Image request region: \(query.region.summary)").font(.caption)
        }
        Button("Load selected optical image") {
          optical.load(python: sar.pythonPath, region: model.sarSearchRegion) { bounds in
            model.dailyImagery = .off
            model.displayStyle = .standard
            model.orient(heading: 0, tilt: 0)
            model.focusSatelliteBounds(bounds)
          }
        }.disabled(optical.busy || optical.scene == nil)
      }
      if optical.busy {
        if let value = optical.fraction { ProgressView(value: value) } else { ProgressView() }
        Button("Cancel optical request") { optical.cancelLoad() }
      }
      if !optical.visibleImages.isEmpty {
        Divider()
        Text("Saved optical images").font(.headline)
        Picker("Image", selection: $optical.imageID) {
          ForEach(optical.visibleImages) { image in
            Text(image.title).tag(Optional(image.id))
          }
        }.disabled(optical.busy || optical.deleting)
        Button(role: .destructive) {
          if let id = optical.imageID { optical.deleteImage(id) }
        } label: {
          Label("Delete saved optical image…", systemImage: "trash")
        }.disabled(optical.image == nil || optical.busy || optical.exporting || optical.deleting)
          .accessibilityIdentifier("delete-optical-image")
        if let image = optical.image, let scene = image.result.scene {
          Text("\(scene.platform) · \(scene.acquiredAt)\n\(scene.id)").font(.caption).textSelection(
            .enabled)
          Text(
            "\(image.result.width ?? 0) × \(image.result.height ?? 0) · EPSG:4326 · \((image.result.validFraction ?? 0)*100, specifier: "%.1f")% valid coverage"
          ).font(.caption)
          Text(
            "Source pixel spacing: \(image.result.sourceResolutionMetres?.first ?? 10, specifier: "%.0f") m; large-region previews are downsampled."
          ).font(.caption)
          if let nsImage = NSImage(
            contentsOf: image.directory.appendingPathComponent("preview.png"))
          {
            Image(nsImage: nsImage).interpolation(optical.pixelGrid ? .none : .medium).resizable()
              .scaledToFit().frame(maxHeight: 220)
          }
          Text(
            "\(image.result.scene?.sensor.name ?? "Optical") · \(image.result.mode ?? "RGB"). Enlarging pixels adds no detail."
          )
          .font(.caption).foregroundStyle(.secondary)
          if let bounds = image.result.bounds, let width = image.result.width,
            let height = image.result.height,
            model.camera.altitude < max(
              8000, max(bounds[2] - bounds[0], bounds[3] - bounds[1]) * 160_000) / 1.5
          {
            Text(
              "Enlarged \(width) × \(height) raster. Zooming further magnifies pixels; it adds no detail."
            )
            .font(.caption).foregroundStyle(.orange)
          }
          Toggle("Pixel-grid display (no smoothing)", isOn: $optical.pixelGrid)
          Text(
            "Pixel-grid affects map display only. Existing saved images may contain earlier resampling; reload the acquisition for the updated rendering."
          )
          .font(.caption).foregroundStyle(.secondary)
          Toggle("Show selected satellite overlay", isOn: $optical.showOverlay)
          Button("Focus optical image") {
            if let bounds = image.result.bounds { model.focusSatelliteBounds(bounds) }
          }
          HStack {
            Button("Export optical PNG…") { optical.export("preview.png") }
            Button("Export display GeoTIFF…") { optical.export("true-color.tif") }
          }.disabled(optical.exporting)
          if image.result.scientific != nil {
            Button("Export selected source values GeoTIFF…") { optical.export("source-values.tif") }
              .disabled(optical.exporting)
          }
          if let wavelengths = image.result.wavelengthsNanometres, !wavelengths.isEmpty {
            Text(
              "Selected wavelengths (nm): "
                + wavelengths.map { String(format: "%.1f", $0) }.joined(separator: ", ")
            ).font(.caption)
          }
          if let limitations = image.result.limitations {
            ForEach(limitations, id: \.self) { Text($0).font(.caption).foregroundStyle(.secondary) }
          }
          HStack {
            Button("Export acquisition metadata…") { optical.export("result.json") }
            Button("Show local optical files") { optical.reveal() }
          }.disabled(optical.exporting)
        }
      }
      Text(
        "Optical imagery is dated, not live. Clouds and shadows may obscure your region. A scene can provide partial coverage. PNG and display TIFF are stretched 8-bit images; use the separate source-value export when available."
      )
      .font(.caption).foregroundStyle(.secondary)
    }
    .onChange(of: optical.imageID) { _, _ in
      model.dailyImagery = .off
      optical.showOverlay = true
    }
    .onChange(of: optical.showOverlay) { _, value in if value { model.dailyImagery = .off } }
  }
}
