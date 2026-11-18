import AppKit
import Darwin
import HeliosCore
import SwiftUI

@MainActor final class SarAcquisitionWorkspace: ObservableObject {
  @Published var startDate = Calendar.current.date(byAdding: .day, value: -60, to: Date())!
  @Published var endDate = Calendar.current.date(byAdding: .day, value: -14, to: Date())!
  @Published var polarization = SentinelPolarization.both
  @Published var mode = "IW"
  @Published var scenes: [SentinelScene] = []
  @Published var selectedID: UUID?
  @Published var query: SentinelQuery?
  @Published var searching = false
  @Published var hasMore = false
  @Published var signingIn = false
  @Published var signedIn = false
  @Published var username = ""
  @Published var password = ""
  @Published var totp = ""
  @Published var downloading = false
  @Published var progress: SentinelDownloadProgress?
  @Published var status = "Choose a region, then search for Sentinel-1 GRD acquisitions."
  @Published var error: String?
  @Published var saved: [SentinelDownloadRecord] = []
  private let catalog = SentinelCatalog()
  private let account = CopernicusAccount()
  private var searchTask: Task<Void, Never>?
  private var loginTask: Task<Void, Never>?
  private var downloadTask: Task<Void, Never>?
  private var searchGeneration = UUID()
  private var downloadGeneration = UUID()
  private var skip = 0
  var selected: SentinelScene? { scenes.first { $0.id == selectedID } }
  static var storage: URL {
    SarWorkspace.storage.deletingLastPathComponent().appendingPathComponent(
      "downloads", isDirectory: true)
  }
  init() { reload() }
  func search(region: SentinelRegion?, more: Bool = false) {
    guard !searching else { return }
    error = nil
    let request: SentinelQuery
    do {
      if more, let query {
        request = query
      } else {
        guard let region else { throw SentinelError.region }
        request = try SentinelQuery(
          region: region, start: startDate, end: endDate, polarization: polarization, mode: mode)
        query = request
        skip = 0
        scenes = []
        selectedID = nil
      }
    } catch {
      self.error = error.localizedDescription
      return
    }
    let offset = more ? skip + 20 : 0
    guard offset <= 980 else {
      hasMore = false
      return
    }
    searchTask?.cancel()
    searchGeneration = UUID()
    let generation = searchGeneration
    searching = true
    status = "Searching Copernicus catalogue…"
    searchTask = Task {
      do {
        let page = try await catalog.search(request, skip: offset)
        try Task.checkCancellation()
        guard generation == searchGeneration else { return }
        let known = Set(scenes.map(\.id))
        scenes += page.scenes.filter { !known.contains($0.id) }
        skip = offset
        hasMore = page.hasMore && offset < 980
        status =
          scenes.isEmpty
          ? "No supported GRD acquisitions found. Try another date range or region."
          : "\(scenes.count) acquisitions found\(page.rejected > 0 ? " · unsupported catalogue entries omitted" : "")."
      } catch {
        guard generation == searchGeneration else { return }
        if !Task.isCancelled {
          self.error = error.localizedDescription
          status = "Catalogue search failed"
        }
      }
      if generation == searchGeneration { searching = false }
    }
  }
  func resetSearch() {
    searchTask?.cancel()
    searchGeneration = UUID()
    searching = false
    error = nil
    scenes = []
    selectedID = nil
    query = nil
    hasMore = false
    if !downloading {
      status = "Region changed. Search to find acquisitions covering this rectangle."
    }
  }
  func cancelSearch() {
    resetSearch()
    status = "Search cancelled."
  }
  func signIn() {
    guard !signingIn, !downloading else { return }
    error = nil
    signingIn = true
    let username = username.trimmingCharacters(in: .whitespacesAndNewlines)
    let password = password
    let totp = totp.trimmingCharacters(in: .whitespacesAndNewlines)
    self.password = ""
    self.totp = ""
    loginTask = Task {
      defer { signingIn = false }
      do {
        try await account.signIn(username: username, password: password, totp: totp)
        try Task.checkCancellation()
        signedIn = true
        status = "Signed in to Copernicus. Select an acquisition to download."
      } catch {
        signedIn = false
        if !Task.isCancelled { self.error = error.localizedDescription }
      }
    }
  }
  func signOut() {
    loginTask?.cancel()
    signedIn = false
    password = ""
    totp = ""
    Task { await account.signOut() }
  }
  func download(onReady: @escaping @MainActor (URL) -> Void) {
    guard !downloading, signedIn, let scene = selected, scene.online, let query else { return }
    let directory = Self.storage.appendingPathComponent(
      scene.id.uuidString.lowercased(), isDirectory: true)
    let destination = directory.appendingPathComponent(scene.archiveFilename)
    if let record = saved.first(where: { $0.id == scene.id }), let existing = file(for: record) {
      onReady(existing)
      return
    }
    error = nil
    downloading = true
    progress = nil
    downloadGeneration = UUID()
    let generation = downloadGeneration
    status = "Preparing download…"
    downloadTask = Task { [self] in
      defer { if downloadGeneration == generation { downloading = false } }
      do {
        let token = try await account.accessToken()
        try Task.checkCancellation()
        let worker = SentinelDownload(destination: destination) { [weak self] progress in
          Task { @MainActor in
            guard let self, self.downloadGeneration == generation, self.downloading else { return }
            self.progress = progress
            self.status = "Downloading Sentinel-1 product…"
          }
        }
        let file = try await worker.run(scene: scene, token: token)
        // Publish provenance only after a complete transfer; interrupted transfers never become saved datasets.
        let record = SentinelDownloadRecord(scene: scene, query: query)
        do {
          let data = try JSONEncoder().encode(record)
          try data.write(to: directory.appendingPathComponent("download.json"), options: .atomic)
        } catch {
          try? FileManager.default.removeItem(at: file)
          throw error
        }
        saved.removeAll { $0.id == scene.id }
        saved.insert(record, at: 0)
        status = "Download complete. The SAR inspector will validate the SAFE product."
        if !Task.isCancelled { onReady(file) }
      } catch {
        if let issue = error as? SentinelError {
          switch issue {
          case .authentication, .signedOut, .download(401), .download(403):
            signedIn = false
            await account.signOut()
          default: break
          }
        }
        if error is CancellationError || Task.isCancelled {
          status = "Download cancelled. Partial files removed."
        } else {
          self.error = error.localizedDescription
          status = "Download failed. No partial dataset retained."
        }
      }
    }
  }
  func cancelDownload() { downloadTask?.cancel() }
  func cancelAll() {
    searchTask?.cancel()
    loginTask?.cancel()
    downloadTask?.cancel()
  }
  func file(for record: SentinelDownloadRecord) -> URL? {
    let directory = Self.storage.appendingPathComponent(record.id.uuidString.lowercased())
    return try? record.archive(in: directory)
  }
  /// Old processing provenance can still refer to product.zip after an in-place rename.
  static func resolveInput(_ input: URL) -> URL {
    guard input.lastPathComponent == "product.zip",
      input.deletingLastPathComponent().deletingLastPathComponent().standardizedFileURL
        == storage.standardizedFileURL,
      let record = try? SarProcessor.read(
        SentinelDownloadRecord.self,
        from: input.deletingLastPathComponent().appendingPathComponent("download.json")),
      let file = try? record.archive(in: input.deletingLastPathComponent())
    else { return input }
    return file
  }

  func reload() {
    let storage = Self.storage
    Task {
      saved = await Task.detached {
        let directories =
          (try? FileManager.default.contentsOfDirectory(
            at: storage, includingPropertiesForKeys: [.isSymbolicLinkKey],
            options: [.skipsHiddenFiles])) ?? []
        for directory in directories.prefix(500)
        where UUID(uuidString: directory.lastPathComponent) != nil {
          guard
            (try? directory.resourceValues(forKeys: [.isSymbolicLinkKey]).isSymbolicLink) == false
          else { continue }
          let files =
            (try? FileManager.default.contentsOfDirectory(
              at: directory, includingPropertiesForKeys: [.isSymbolicLinkKey])) ?? []
          for file in files where file.lastPathComponent.hasPrefix(".partial-") {
            let parts = file.lastPathComponent.split(separator: "-", maxSplits: 2)
            if parts.count == 3, let pid = Int32(parts[1]), pid > 0,
              UUID(uuidString: String(parts[2])) != nil,
              kill(pid, 0) != 0, errno == ESRCH
            {
              try? FileManager.default.removeItem(at: file)
            }
          }
        }
        return directories.prefix(500).compactMap { directory -> SentinelDownloadRecord? in
          guard UUID(uuidString: directory.lastPathComponent) != nil,
            (try? directory.resourceValues(forKeys: [.isSymbolicLinkKey]).isSymbolicLink) == false,
            let record = try? SarProcessor.read(
              SentinelDownloadRecord.self, from: directory.appendingPathComponent("download.json")),
            record.id.uuidString.lowercased() == directory.lastPathComponent.lowercased(),
            SentinelScene.supportedName(record.scene.name),
            (try? record.archive(in: directory)) != nil
          else { return nil }
          return record
        }.sorted { $0.downloadedAt > $1.downloadedAt }
      }.value
    }
  }
}

struct SarAcquisitionTools: View {
  @ObservedObject var acquisition: SarAcquisitionWorkspace
  @ObservedObject var model: WorkspaceModel
  var onReady: @MainActor (URL) -> Void
  @State private var west = 0.0
  @State private var south = 0.0
  @State private var east = 0.0
  @State private var north = 0.0
  @State private var editingBounds = false
  var body: some View {
    VStack(alignment: .leading, spacing: 12) {
      Text("Find Sentinel-1 acquisitions").font(.headline)
      HStack {
        Button("Select region on globe…") { model.beginSarRegionSelection() }
        Button("Use current view") { model.setSarRegionToView() }
      }.disabled(acquisition.downloading)
      if let region = model.sarSearchRegion {
        Text("Search bounds (latitude, longitude): \(region.summary)").font(.caption).textSelection(
          .enabled)
      } else {
        Text("Select two opposite corners of a rectangle on the globe.").font(.caption)
          .foregroundStyle(.secondary)
      }
      DisclosureGroup("Enter exact bounds", isExpanded: $editingBounds) {
        HStack {
          Text("West")
          TextField("West longitude", value: $west, format: .number)
          Text("East")
          TextField("East longitude", value: $east, format: .number)
        }
        HStack {
          Text("South")
          TextField("South latitude", value: $south, format: .number)
          Text("North")
          TextField("North latitude", value: $north, format: .number)
        }
        Button("Apply bounds") {
          do {
            model.setSarSearchRegion(
              try SentinelRegion(west: west, south: south, east: east, north: north))
          } catch { acquisition.error = error.localizedDescription }
        }
      }.disabled(acquisition.downloading)
      HStack {
        DatePicker("From (UTC)", selection: $acquisition.startDate, displayedComponents: .date)
        DatePicker("To (UTC)", selection: $acquisition.endDate, displayedComponents: .date)
      }.environment(\.timeZone, TimeZone(secondsFromGMT: 0)!)
      HStack {
        Picker("Mode", selection: $acquisition.mode) {
          Text("IW").tag("IW")
          Text("EW").tag("EW")
          Text("SM").tag("SM")
        }
        Picker("Polarization", selection: $acquisition.polarization) {
          ForEach(SentinelPolarization.allCases, id: \.self) { Text($0.rawValue).tag($0) }
        }
      }
      HStack {
        Button("Search acquisitions") { acquisition.search(region: model.sarSearchRegion) }
          .disabled(
            acquisition.searching || model.sarSearchRegion == nil || acquisition.downloading)
        if acquisition.searching {
          ProgressView().controlSize(.small)
          Button("Cancel search") { acquisition.cancelSearch() }
        }
      }
      Text(
        "Downloads contain complete GRD SAFE scenes intersecting the selected region. Region selection does not crop the download. Product sizes can be several gigabytes."
      )
      .font(.caption).foregroundStyle(.secondary)
      if let query = acquisition.query, !acquisition.scenes.isEmpty {
        Text(
          "Results: \(query.mode) · \(query.polarization.rawValue) · \(query.start.formatted(date: .abbreviated, time: .omitted)) to \(query.end.formatted(date: .abbreviated, time: .omitted))"
        )
        .font(.caption).foregroundStyle(.secondary)
        ScrollView {
          LazyVStack(alignment: .leading, spacing: 8) {
            ForEach(acquisition.scenes) { scene in
              Button {
                acquisition.selectedID = scene.id
                model.sarSceneFootprints = scene.footprint
              } label: {
                HStack(alignment: .top) {
                  Image(
                    systemName: acquisition.selectedID == scene.id
                      ? "largecircle.fill.circle" : "circle")
                  VStack(alignment: .leading, spacing: 3) {
                    Text(
                      "\(scene.satellite) · \(scene.acquisition.prefix(19).replacingOccurrences(of: "T", with: " ")) UTC · \(scene.polarizations)"
                    )
                    Text(
                      "\(scene.mode) · \(scene.orbit) · \(ByteCountFormatter.string(fromByteCount: scene.contentLength, countStyle: .file)) catalogue size\(scene.online ? "" : " · unavailable")"
                    )
                    .font(.caption).foregroundStyle(.secondary)
                  }
                  Spacer()
                }.padding(6).contentShape(Rectangle())
              }.buttonStyle(.plain)
            }
          }
        }.frame(maxHeight: 200)
        if acquisition.hasMore {
          Button("Load 20 more") { acquisition.search(region: model.sarSearchRegion, more: true) }
            .disabled(acquisition.searching || acquisition.downloading)
        }
      }
      if let scene = acquisition.selected {
        Text(scene.name).font(.caption).textSelection(.enabled)
        HStack {
          Button("Show scene footprint on globe") { model.focusSarScene(scene) }
          Button("Download & import") { acquisition.download(onReady: onReady) }
            .disabled(
              !acquisition.signedIn || acquisition.downloading || acquisition.searching
                || !scene.online || model.sar.busy)
        }
      }
      if acquisition.downloading {
        if let fraction = acquisition.progress?.fraction {
          ProgressView(value: fraction)
        } else {
          ProgressView()
        }
        HStack {
          if let p = acquisition.progress {
            Text(
              "\(ByteCountFormatter.string(fromByteCount: p.received, countStyle: .file))\(p.expected.map { " / " + ByteCountFormatter.string(fromByteCount: $0, countStyle: .file) } ?? " downloaded")"
            ).font(.caption)
          }
          Button("Cancel download") { acquisition.cancelDownload() }
        }
      }
      Text(acquisition.status).font(.callout)
      if let error = acquisition.error {
        Text(error).foregroundStyle(.orange).textSelection(.enabled)
      }
      DisclosureGroup(
        acquisition.signedIn
          ? "Copernicus account · signed in" : "Copernicus account · sign in to download"
      ) {
        VStack(alignment: .leading, spacing: 8) {
          if acquisition.signedIn {
            Button("Sign out") { acquisition.signOut() }.disabled(acquisition.downloading)
          } else {
            TextField("Copernicus username / email", text: $acquisition.username)
            SecureField("Password", text: $acquisition.password)
            TextField("Two-factor code (if enabled)", text: $acquisition.totp)
            HStack {
              Button("Sign in") { acquisition.signIn() }.disabled(
                acquisition.signingIn || acquisition.username.isEmpty
                  || acquisition.password.isEmpty)
              if acquisition.signingIn { ProgressView().controlSize(.small) }
              Link(
                "Create free account", destination: URL(string: "https://dataspace.copernicus.eu/")!
              )
            }
          }
          Text(
            "Searching needs no account. Downloads use your free Copernicus account. Passwords and session tokens are kept only in memory; sign in again after restarting Helios."
          )
          .font(.caption).foregroundStyle(.secondary)
        }
      }
      if !acquisition.saved.isEmpty {
        DisclosureGroup("Downloaded products (\(acquisition.saved.count))") {
          ForEach(acquisition.saved) { record in
            HStack {
              Text(
                "\(record.scene.satellite) · \(record.scene.acquisition.prefix(19)) UTC · orbit \(record.scene.relativeOrbit.map(String.init) ?? "?") · \(record.query.region.summary)"
              ).font(.caption)
              Button("Use for processing") {
                if let file = acquisition.file(for: record) { onReady(file) }
              }
              .disabled(model.sar.busy)
              Button("Show files") {
                if let file = acquisition.file(for: record) {
                  NSWorkspace.shared.activateFileViewerSelecting([file])
                }
              }
            }
          }
        }
      }
    }
    .onAppear { syncBounds() }
    .onChange(of: model.sarSearchRegion) { _, _ in syncBounds() }
  }
  private func syncBounds() {
    guard let region = model.sarSearchRegion else { return }
    west = region.west
    south = region.south
    east = region.east
    north = region.north
  }
}
