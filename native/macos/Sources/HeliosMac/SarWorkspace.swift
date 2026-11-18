import AppKit
import Darwin
import HeliosCore
import SwiftUI
import UniformTypeIdentifiers

struct SavedSarJob: Identifiable, Sendable {
  var id: String { directory.lastPathComponent }
  var directory: URL
  var result: SarResult
  var title: String {
    guard let product = result.metadata else {
      return "\(result.inputName) · \(result.createdAt.prefix(19))"
    }
    return
      "\(product.satellite) · \(product.acquisitionStart.prefix(19)) UTC · \(product.mode) · orbit \(product.relativeOrbit.map(String.init) ?? "?")"
  }
}
@MainActor final class SarWorkspace: ObservableObject {
  @Published var input: URL?
  @Published var product: SarProduct?
  @Published var options = SarOptions()
  @Published var status = "Select a Sentinel-1 GRD SAFE product."
  @Published var state = SarState.completed
  @Published var fraction: Double?
  @Published var error: String?
  @Published var jobs: [SavedSarJob] = []
  @Published var selectedJob: String?
  @Published var selectedOutput = "VV"
  @Published var overlayEnabled = true
  @Published var exportBusy = false
  @Published var workflow = "Find & download"
  @Published var pythonPath: String {
    didSet { UserDefaults.standard.set(pythonPath, forKey: "sar.python") }
  }
  @Published var gptPath: String {
    didSet { UserDefaults.standard.set(gptPath, forKey: "sar.gpt") }
  }
  private var task: Task<Void, Never>?
  private var generation = UUID()
  var busy: Bool { state.isActive }
  static var storage: URL {
    SatelliteStorage.root.appendingPathComponent("SAR/jobs", isDirectory: true)
  }
  var currentJob: SavedSarJob? { jobs.first { $0.id == selectedJob } }
  var output: SarOutput? { currentJob?.result.outputs.first { $0.id == selectedOutput } }
  var overlay: SarOverlay? {
    guard overlayEnabled, let job = currentJob, job.result.state == .completed,
      let output, output.hasValidBounds, let file = output.file(output.preview, in: job.directory)
    else { return nil }
    return SarOverlay(
      preview: file, bounds: output.bounds,
      label:
        "Sentinel-1 \(output.id) radar backscatter · \(job.result.metadata?.acquisitionStart ?? "")"
    )
  }
  init() {
    // Validation profiles isolate datasets, while reusing installed runtimes.
    let support = SatelliteStorage.defaultRoot.appendingPathComponent("SAR", isDirectory: true)
    pythonPath =
      UserDefaults.standard.string(forKey: "sar.python")
      ?? support.appendingPathComponent("runtime/bin/python3").path
    gptPath = UserDefaults.standard.string(forKey: "sar.gpt") ?? "/Applications/snap/bin/gpt"
    reload()
  }
  private var processor: SarProcessor? {
    guard
      let worker = Bundle.module.url(
        forResource: "sentinel1", withExtension: "py", subdirectory: "Resources/SAR")
    else { return nil }
    return SarProcessor(python: URL(fileURLWithPath: pythonPath), worker: worker)
  }
  func chooseInput() {
    let panel = NSOpenPanel()
    panel.canChooseDirectories = true
    panel.canChooseFiles = true
    panel.allowsMultipleSelection = false
    let downloads = SarAcquisitionWorkspace.storage
    try? FileManager.default.createDirectory(at: downloads, withIntermediateDirectories: true)
    panel.directoryURL = downloads
    panel.message = "Select a Sentinel-1 GRD .SAFE folder, manifest.safe, or zipped SAFE product."
    if panel.runModal() == .OK, let input = panel.url { inspect(input) }
  }
  func chooseEngine(python: Bool) {
    let panel = NSOpenPanel()
    panel.message =
      python
      ? "Select the Python interpreter in a SAR environment with Rasterio, NumPy and Pillow."
      : "Select SNAP's bin/gpt executable (Microwave Toolbox required)."
    if panel.runModal() == .OK, let url = panel.url {
      if python { pythonPath = url.path } else { gptPath = url.path }
    }
  }
  private func update(_ progress: SarProgress, generation: UUID) {
    guard generation == self.generation, state.allows(progress.state) else { return }
    state = progress.state
    status = progress.state == .failed ? "Processing failed" : progress.stage
    fraction = progress.progress.flatMap { $0.isFinite ? min(1, max(0, $0)) : nil }
  }
  func inspect(_ input: URL) {
    guard !busy else { return }
    task?.cancel()
    let input = SarAcquisitionWorkspace.resolveInput(input)
    self.input = input
    product = nil
    error = nil
    state = .queued
    status = "Queued for validation"
    fraction = nil
    generation = UUID()
    let token = generation
    guard let processor else {
      error = "The bundled SAR worker is missing."
      state = .failed
      return
    }
    task = Task { [self] in
      let temporary = FileManager.default.temporaryDirectory.appendingPathComponent(
        "sar-inspect-\(UUID().uuidString).json")
      defer { try? FileManager.default.removeItem(at: temporary) }
      do {
        let product = try await processor.inspect(input: input, destination: temporary) {
          [weak self] progress in
          Task { @MainActor in self?.update(progress, generation: token) }
        }
        try Task.checkCancellation()
        guard generation == token else { return }
        self.product = product
        options.polarizations = product.polarizations
        state = .completed
        status = "Supported GRD product · \(product.polarizations.joined(separator: ", "))"
      } catch {
        guard generation == token else { return }
        state = .failed
        status = "Validation failed"
        self.error =
          error is CancellationError ? "Validation cancelled." : error.localizedDescription
      }
    }
  }
  func start() {
    guard !busy, let input, product != nil, !options.polarizations.isEmpty, let processor else {
      return
    }
    error = nil
    state = .queued
    status = "Queued for SAR processing"
    fraction = 0
    generation = UUID()
    let token = generation
    let options = options
    let gpt = URL(fileURLWithPath: gptPath)
    let directory = Self.storage.appendingPathComponent(UUID().uuidString, isDirectory: true)
    task = Task { [self] in
      do {
        let result = try await processor.process(
          input: SarAcquisitionWorkspace.resolveInput(input), destination: directory, gpt: gpt,
          options: options
        ) { [weak self] progress in
          Task { @MainActor in self?.update(progress, generation: token) }
        }
        try Task.checkCancellation()
        guard generation == token else { return }
        jobs.insert(SavedSarJob(directory: directory, result: result), at: 0)
        selectedJob = directory.lastPathComponent
        selectedOutput = result.outputs.first?.id ?? "VV"
        state = .completed
        fraction = 1
        status = "Radar backscatter ready"
      } catch {
        guard generation == token else { return }
        state = .failed
        status = "Processing failed"
        self.error =
          error is CancellationError
          ? "Processing cancelled; partial outputs were removed." : error.localizedDescription
        if FileManager.default.fileExists(
          atPath: directory.appendingPathComponent("result.json").path)
        {
          selectedJob = directory.lastPathComponent
        }
        reload()
      }
    }
  }
  func cancel() { task?.cancel() }
  func reload() {
    let root = Self.storage
    Task { [self] in
      let saved = await Task.detached {
        let directories =
          (try? FileManager.default.contentsOfDirectory(
            at: root,
            includingPropertiesForKeys: [.isSymbolicLinkKey], options: [.skipsHiddenFiles])) ?? []
        return directories.filter { UUID(uuidString: $0.lastPathComponent) != nil }
          .prefix(200).compactMap { directory -> SavedSarJob? in
            guard
              (try? directory.resourceValues(forKeys: [.isSymbolicLinkKey]).isSymbolicLink) == false
            else { return nil }
            try? SarJobStorage.recoverInterrupted(in: directory) { pid in
              kill(pid, 0) == 0 || errno != ESRCH
            }
            guard
              let result = try? SarProcessor.read(
                SarResult.self, from: directory.appendingPathComponent("result.json")),
              result.version == 1
            else { return nil }
            return SavedSarJob(directory: directory, result: result)
          }.sorted { $0.result.createdAt > $1.result.createdAt }
      }.value
      if !busy {
        jobs = saved
        if selectedJob == nil {
          selectedJob = saved.first(where: { $0.result.state == .completed })?.id
        }
      }
    }
  }
  func export(_ name: String) {
    guard !exportBusy, let job = currentJob, let output,
      let source = output.file(name, in: job.directory)
    else { return }
    let panel = NSSavePanel()
    panel.nameFieldStringValue = name
    panel.allowedContentTypes = name.hasSuffix(".png") ? [.png] : [.tiff]
    guard panel.runModal() == .OK, let target = panel.url else { return }
    exportBusy = true
    Task {
      defer { exportBusy = false }
      do {
        try await Task.detached {
          let staged = target.deletingLastPathComponent().appendingPathComponent(
            ".sar-export-\(UUID().uuidString)")
          defer { try? FileManager.default.removeItem(at: staged) }
          try FileManager.default.copyItem(at: source, to: staged)
          if FileManager.default.fileExists(atPath: target.path) {
            _ = try FileManager.default.replaceItemAt(target, withItemAt: staged)
          } else {
            try FileManager.default.moveItem(at: staged, to: target)
          }
        }.value
      } catch {
        self.error =
          "The result could not be exported. Check destination permissions and free disk space."
      }
    }
  }
  func revealDiagnostics() {
    if let job = currentJob { NSWorkspace.shared.activateFileViewerSelecting([job.directory]) }
  }
}

struct SarTools: View {
  @ObservedObject var sar: SarWorkspace
  @ObservedObject var model: WorkspaceModel
  var body: some View {
    VStack(alignment: .leading, spacing: 12) {
      Text("Sentinel-1 radar backscatter").font(.headline)
      Text(
        "Radar measures microwave scattering, not visible light. Calm water often appears dark; strong urban or metallic returns can be bright. Viewing geometry and polarization affect interpretation."
      ).font(.caption).foregroundStyle(.secondary)
      Picker("SAR workflow", selection: $sar.workflow) {
        Text("Find & download").tag("Find & download")
        Text("Process & results").tag("Process & results")
      }.pickerStyle(.segmented)
      if sar.workflow == "Find & download" {
        SarAcquisitionTools(acquisition: model.sarAcquisition, model: model) { file in
          sar.inspect(file)
          sar.workflow = "Process & results"
        }
      } else {
        Button("Import GRD SAFE / ZIP…") { sar.chooseInput() }.disabled(sar.busy)
        if let input = sar.input {
          Text(input.lastPathComponent).font(.caption).textSelection(.enabled)
        }
        if let p = sar.product {
          Text("\(p.satellite) · \(p.mode) · \(p.productType) · \(p.orbitDirection)")
          Text("\(p.acquisitionStart) — \(p.acquisitionEnd)").font(.caption)
          if let orbit = p.relativeOrbit { Text("Relative orbit: \(orbit)").font(.caption) }
          ForEach(p.bands, id: \.polarization) { band in
            Toggle(
              "\(band.polarization) · \(band.width) × \(band.height)",
              isOn: Binding(
                get: { sar.options.polarizations.contains(band.polarization) },
                set: { selected in
                  sar.options.polarizations.removeAll { $0 == band.polarization }
                  if selected { sar.options.polarizations.append(band.polarization) }
                }))
          }.disabled(sar.busy)
        }
        Group {
          Toggle(
            "Apply orbit correction (precise preferred; restituted fallback)",
            isOn: $sar.options.applyOrbit)
          Toggle("Refined Lee speckle reduction", isOn: $sar.options.speckle)
          HStack {
            Text("Output spacing (m)")
            Picker("Output spacing", selection: $sar.options.pixelSpacing) {
              ForEach([10.0, 20.0, 40.0, 100.0], id: \.self) {
                Text(String(format: "%.0f", $0)).tag($0)
              }
            }.labelsHidden().frame(width: 100)
          }
          HStack {
            Text("Display percentiles")
            TextField("Low", value: $sar.options.lowPercentile, format: .number).frame(width: 60)
            Text("to")
            TextField("High", value: $sar.options.highPercentile, format: .number).frame(width: 60)
          }
        }.disabled(sar.busy)
        HStack {
          Button("Process Sigma0") { sar.start() }.disabled(
            sar.busy || sar.product == nil || sar.options.polarizations.isEmpty)
          if sar.busy { Button("Cancel") { sar.cancel() } }
        }
        if sar.busy {
          if let fraction = sar.fraction { ProgressView(value: fraction) } else { ProgressView() }
        }
        Text(sar.status).font(.callout)
        if let error = sar.error { Text(error).foregroundStyle(.orange).textSelection(.enabled) }
        Text(
          "Calibration and range-Doppler terrain correction run in SNAP. Copernicus DEM/orbit downloads may require network access. The exported Sigma0 TIFF remains floating point; preview stretching changes display only."
        ).font(.caption).foregroundStyle(.secondary)
        Divider()
        if !sar.jobs.isEmpty {
          Picker("Saved processing", selection: $sar.selectedJob) {
            Text("Select result").tag(String?.none)
            ForEach(sar.jobs) { job in
              Text("\(job.title) · \(job.result.state.rawValue)").tag(Optional(job.id))
            }
          }
        }
        if let job = sar.currentJob {
          if let metadata = job.result.metadata {
            Text("\(metadata.satellite) · \(metadata.mode) · \(metadata.acquisitionStart)").font(
              .caption)
          }
          if job.result.state == .completed {
            Picker("Output", selection: $sar.selectedOutput) {
              ForEach(job.result.outputs) { Text($0.id).tag($0.id) }
            }.onChange(of: sar.selectedJob) { _, _ in
              sar.selectedOutput = sar.currentJob?.result.outputs.first?.id ?? "VV"
            }
            if let output = sar.output, output.hasValidDisplay,
              let file = output.file(output.preview, in: job.directory),
              let image = NSImage(contentsOf: file)
            {
              Image(nsImage: image).resizable().scaledToFit().frame(maxHeight: 240)
              Text(
                "\(output.width) × \(output.height) · EPSG:4326 · output spacing \(job.result.options?.pixelSpacing ?? 20, specifier: "%.0f") m nominal"
              ).font(.caption)
              Text(
                output.id == "Composite"
                  ? "False color: red VV, green VH, blue VV − VH (dB). Each channel is independently stretched."
                  : "\(output.id) Sigma0 dB grayscale visualization"
              ).font(.caption)
              Text(
                output.displayRanges.map { String(format: "%.1f to %.1f dB", $0[0], $0[1]) }.joined(
                  separator: " · ")
              ).font(.caption)
              Toggle("Show radar overlay on globe", isOn: $sar.overlayEnabled)
              HStack {
                Button("Focus result") {
                  let b = output.bounds
                  if output.hasValidBounds,
                    let center = Coordinate(
                      latitude: (b[1] + b[3]) / 2, longitude: (b[0] + b[2]) / 2)
                  {
                    model.fly(
                      to: center, altitude: max(20_000, max(b[2] - b[0], b[3] - b[1]) * 160_000))
                  }
                }
                if let scientific = output.scientific {
                  Button("Export Sigma0 TIFF…") { sar.export(scientific) }
                }
                if let db = output.db { Button("Export dB TIFF…") { sar.export(db) } }
              }.disabled(sar.exportBusy)
              HStack {
                Button("Export display TIFF…") { sar.export(output.visualization) }
                Button("Export PNG…") { sar.export(output.preview) }
              }.disabled(sar.exportBusy)
            }
          } else {
            if job.result.error != sar.error {
              Text(job.result.error ?? "Interrupted job; no completed radar layer is available.")
                .foregroundStyle(.orange)
            }
          }
          Button("Show local files / diagnostics") { sar.revealDiagnostics() }
        }
        DisclosureGroup("Processing engine setup") {
          VStack(alignment: .leading) {
            TextField("SAR Python executable", text: $sar.pythonPath)
            Button("Choose Python…") { sar.chooseEngine(python: true) }
            TextField("SNAP GPT executable", text: $sar.gptPath)
            Button("Choose GPT…") { sar.chooseEngine(python: false) }
            Text(
              "Use an Apple Silicon Python environment with Rasterio, NumPy and Pillow, and SNAP with the Microwave Toolbox. No processing tools are installed automatically."
            ).font(.caption)
            Link(
              "ESA SNAP installation",
              destination: URL(string: "https://step.esa.int/main/download/snap-download/")!)
          }.disabled(sar.busy)
        }
      }
    }
  }
}
