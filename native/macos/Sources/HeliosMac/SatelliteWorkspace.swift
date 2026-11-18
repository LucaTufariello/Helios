import AppKit
import Darwin
import HeliosCore
import SwiftUI

struct SavedChangeReview: Identifiable, Sendable {
  var id: UUID
  var directory: URL
  var result: ChangeReviewResult
}

@MainActor final class SatelliteWorkspace: ObservableObject {
  @Published var page = "SAR workflow"
  @Published var sites: [InfrastructureSite] = []
  @Published var siteID: UUID?
  @Published var siteName = ""
  @Published var siteKind = "Infrastructure"
  @Published var beforeID: String?
  @Published var afterID: String?
  @Published var options = ChangeReviewOptions()
  @Published var reviews: [SavedChangeReview] = []
  @Published var reviewID: UUID?
  @Published var candidateID: String?
  @Published var notes: ChangeReviewNotes?
  @Published var view = "Change"
  @Published var showOverlay = true
  @Published var state = SarState.completed
  @Published var status = "Save a site and select two comparable, completed SAR acquisitions."
  @Published var fraction: Double?
  @Published var error: String?
  @Published var storageError: String?
  @Published var exportBusy = false
  private var task: Task<Void, Never>?
  private var generation = UUID()
  var busy: Bool { state.isActive }
  static var storage: URL {
    SatelliteStorage.root
      .appendingPathComponent("Satellite", isDirectory: true)
  }
  static var sitesFile: URL { storage.appendingPathComponent("sites.json") }
  var site: InfrastructureSite? { sites.first { $0.id == siteID } }
  var review: SavedChangeReview? { reviews.first { $0.id == reviewID } }
  var candidate: ChangeCandidate? { review?.result.candidates?.first { $0.id == candidateID } }
  var overlay: SarOverlay? {
    guard showOverlay, let review,
      review.result.state == .completed, let bounds = review.result.bounds,
      ChangeReviewResult.validBounds(bounds), ["Before", "After", "Change"].contains(view)
    else { return nil }
    let label: String
    if view == "Change", let evidence = review.result.evidence {
      label =
        "Radar change · \(evidence[0].metadata.acquisitionStart) → \(evidence[1].metadata.acquisitionStart) · dB log ratio"
    } else {
      let index = view == "Before" ? 0 : 1
      label =
        "\(view) · \(review.result.evidence?[index].metadata.acquisitionStart ?? "") · averaged radar backscatter"
    }
    return SarOverlay(
      preview: review.directory.appendingPathComponent(view + ".png"), bounds: bounds, label: label)
  }
  init() {
    do { sites = try SatelliteDocuments.loadSites(from: Self.sitesFile) } catch {
      storageError = "Saved sites could not be read. The existing file has been preserved."
    }
    reload()
  }
  func saveSite(region: SentinelRegion?) {
    guard let region else {
      error = "Draw a boundary or use the current map view first."
      return
    }
    let site = InfrastructureSite(name: siteName, kind: siteKind, region: region)
    guard site.isValid else {
      error = "Enter a site name of 1–120 characters."
      return
    }
    do {
      let next = sites + [site]
      try SatelliteDocuments.saveSites(next, to: Self.sitesFile)
      sites = next
      siteID = site.id
      siteName = ""
      error = nil
    } catch { self.error = "Could not save the site: \(error.localizedDescription)" }
  }
  func updateSiteName() {
    guard let selected = siteID, let index = sites.firstIndex(where: { $0.id == selected }) else {
      return
    }
    var next = sites
    next[index].name = siteName.trimmingCharacters(in: .whitespacesAndNewlines)
    next[index].kind = siteKind
    guard next[index].isValid else {
      error = "Enter a site name of 1–120 characters."
      return
    }
    do {
      try SatelliteDocuments.saveSites(next, to: Self.sitesFile)
      sites = next
      siteName = ""
      error = nil
    } catch { self.error = "The site could not be updated." }
  }
  func deleteSite(_ id: UUID) {
    guard !busy, let selected = sites.first(where: { $0.id == id }) else { return }
    let alert = NSAlert()
    alert.messageText = "Delete saved site “\(selected.name)”?"
    alert.informativeText =
      "This removes the saved name and boundary. Existing reviews, review notes and satellite images will be kept."
    alert.addButton(withTitle: "Delete site")
    alert.addButton(withTitle: "Cancel")
    guard alert.runModal() == .alertFirstButtonReturn else { return }
    do {
      sites = try SatelliteDocuments.deleteSite(id: id, from: Self.sitesFile)
      if siteID == id {
        siteID = nil
        siteName = ""
      }
      error = nil
      status = "Saved site deleted. Existing reviews and imagery were kept."
    } catch { self.error = "The saved site could not be deleted. The site list was preserved." }
  }
  func selectReview(_ id: UUID?) {
    reviewID = id
    candidateID = nil
    notes = nil
    error = nil
    guard let review else { return }
    siteID = sites.first(where: { $0.id == review.result.site?.id })?.id
    let file = review.directory.appendingPathComponent("notes.json")
    if FileManager.default.fileExists(atPath: file.path) {
      do {
        let saved = try SarProcessor.read(ChangeReviewNotes.self, from: file)
        let ids = Set(review.result.candidates?.map(\.id) ?? [])
        guard saved.isValid, saved.reviewID == review.id,
          saved.candidates.keys.allSatisfy(ids.contains)
        else { throw FeedError.malformed }
        notes = saved
      } catch {
        self.error =
          "Saved review notes could not be read and have been preserved. Show local files to recover them."
      }
    } else {
      notes = ChangeReviewNotes(reviewID: review.id)
    }
    if review.result.state == .failed { error = review.result.error }
    view = "Change"
  }
  func saveDecision(_ value: CandidateReview) {
    guard let review, let candidateID, let current = notes, value.isValid else {
      error = "Notes exceed the limit or the saved notes file needs recovery."
      return
    }
    var updated = current
    var value = value
    value.updatedAt = Date()
    updated.candidates[candidateID] = value
    do {
      try SatelliteDocuments.save(
        updated, to: review.directory.appendingPathComponent("notes.json"))
      notes = updated
      status = "Review decision saved locally."
      error = nil
    } catch { self.error = "The review decision could not be saved." }
  }
  func start(sar: SarWorkspace) {
    guard !busy, let site, let before = sar.jobs.first(where: { $0.id == beforeID }),
      let after = sar.jobs.first(where: { $0.id == afterID }), options.isValid
    else {
      error = "Save/select a site, choose two acquisitions and check the analysis settings."
      return
    }
    if let issue = ChangeReviewCompatibility.issue(
      before: before.result, after: after.result, polarization: options.polarization)
    {
      error = issue
      return
    }
    guard
      let worker = Bundle.module.url(
        forResource: "sentinel1", withExtension: "py", subdirectory: "Resources/SAR")
    else {
      error = "The bundled SAR worker is unavailable."
      return
    }
    let processor = SarProcessor(python: URL(fileURLWithPath: sar.pythonPath), worker: worker)
    let request = ChangeReviewRequest(
      site: site, options: options, before: before.directory, after: after.directory)
    let id = UUID()
    let directory = Self.storage.appendingPathComponent(
      "reviews/\(id.uuidString)", isDirectory: true)
    let requestFile = Self.storage.appendingPathComponent(".request-\(id.uuidString).json")
    generation = id
    state = .queued
    status = "Queued for infrastructure change review"
    fraction = nil
    error = nil
    task = Task { [self] in
      defer { try? FileManager.default.removeItem(at: requestFile) }
      do {
        try SatelliteDocuments.save(request, to: requestFile)
        let result = try await processor.compare(request: requestFile, destination: directory) {
          update in
          Task { @MainActor [weak self] in
            guard let self, self.generation == id, self.state.allows(update.state) else { return }
            self.state = update.state
            self.status = update.stage
            self.fraction = update.progress
          }
        }
        guard generation == id else { return }
        reviews.insert(SavedChangeReview(id: id, directory: directory, result: result), at: 0)
        selectReview(id)
        state = .completed
        fraction = 1
        status = "Review ready: \(result.candidates?.count ?? 0) radar-change candidates."
      } catch {
        guard generation == id else { return }
        state = .failed
        fraction = nil
        status = "Change review failed"
        self.error = error is CancellationError ? "Review cancelled." : error.localizedDescription
        reload()
      }
    }
  }
  func cancel() {
    task?.cancel()
  }
  func reload() {
    let root = Self.storage.appendingPathComponent("reviews", isDirectory: true)
    Task {
      let loaded = await Task.detached(priority: .utility) {
        let files =
          (try? FileManager.default.contentsOfDirectory(
            at: root, includingPropertiesForKeys: nil)) ?? []
        return files.prefix(500).compactMap { directory -> SavedChangeReview? in
          guard let id = UUID(uuidString: directory.lastPathComponent),
            var result = try? SarProcessor.read(
              ChangeReviewResult.self, from: directory.appendingPathComponent("result.json")),
            result.isValid
          else { return nil }
          if result.state.isActive, let pid = result.workerPID, pid > 0,
            kill(pid, 0) != 0 && errno == ESRCH
          {
            for file
              in (try? FileManager.default.contentsOfDirectory(
                at: directory, includingPropertiesForKeys: nil)) ?? []
            where ["tif", "png", "tmp"].contains(file.pathExtension) {
              try? FileManager.default.removeItem(at: file)
            }
            result.state = .failed
            result.error =
              "This review was interrupted; partial outputs were removed. Run a new comparison."
            try? SatelliteDocuments.save(
              result, to: directory.appendingPathComponent("result.json"))
          }
          return SavedChangeReview(id: id, directory: directory, result: result)
        }.sorted { $0.result.createdAt > $1.result.createdAt }
      }.value
      reviews = loaded
    }
  }
  func reveal() {
    NSWorkspace.shared.activateFileViewerSelecting([review?.directory ?? Self.storage])
  }
  func exportRaster(_ name: String) {
    guard let review,
      [
        "delta-db.tif", "candidate-mask.tif", "change-display.tif", "Change.png", "Before.png",
        "After.png",
      ].contains(name), !exportBusy
    else { return }
    let panel = NSSavePanel()
    panel.nameFieldStringValue = "\(review.result.site?.name ?? "site")-\(name)"
    guard panel.runModal() == .OK, let target = panel.url else { return }
    let source = review.directory.appendingPathComponent(name)
    exportBusy = true
    Task {
      defer { exportBusy = false }
      do {
        try await Task.detached(priority: .utility) {
          // Atomic destination replacement, preserving any existing file on failure.
          let temp = target.deletingLastPathComponent().appendingPathComponent(
            ".helios-\(UUID().uuidString).tmp")
          defer { try? FileManager.default.removeItem(at: temp) }
          try FileManager.default.copyItem(at: source, to: temp)
          if FileManager.default.fileExists(atPath: target.path) {
            _ = try FileManager.default.replaceItemAt(target, withItemAt: temp)
          } else {
            try FileManager.default.moveItem(at: temp, to: target)
          }
        }.value
      } catch { self.error = "Could not export the review raster." }
    }
  }
  func exportEvidence() {
    guard let review, review.result.state == .completed, let notes else { return }
    let panel = NSSavePanel()
    panel.nameFieldStringValue = "\(review.result.site?.name ?? "site")-review.json"
    guard panel.runModal() == .OK, let url = panel.url else { return }
    struct EvidencePackage: Encodable {
      let version = 1
      let exportedAt = ISO8601DateFormatter().string(from: Date())
      let result: ChangeReviewResult
      let notes: ChangeReviewNotes
      let interpretation =
        "Radar-change candidates with analyst decisions; not validated infrastructure classification."
    }
    do {
      var snapshot = review.result
      snapshot.workerPID = nil
      try SatelliteDocuments.save(EvidencePackage(result: snapshot, notes: notes), to: url)
    } catch { self.error = "Could not export the review evidence." }
  }
}
