import AppKit
import GodEyeCore
import SwiftUI

extension MissionWorkspace {
  var brief: MissionBrief { plan?.brief ?? MissionBrief() }
  @discardableResult func editBrief(_ message: String, _ edit: (inout MissionBrief) -> Void) -> Bool
  {
    guard !dirty || save(), let index = plans.firstIndex(where: { $0.id == selectedID }) else {
      return false
    }
    var next = plans
    var brief = next[index].brief ?? MissionBrief()
    edit(&brief)
    brief.record(message)
    next[index].brief = brief
    next[index].updatedAt = Date()
    do {
      try MissionDocuments.save(next, to: Self.file)
      plans = next
      error = nil
      status = message
      return true
    } catch {
      self.error = "This change could not be saved. Check field limits and available disk space."
      return false
    }
  }
  @discardableResult func addArea(name: String, purpose: String, region: SentinelRegion) -> Bool {
    let area = MissionArea(
      name: name.trimmingCharacters(in: .whitespacesAndNewlines), purpose: purpose, region: region)
    return editBrief("Area saved: \(area.name)") { $0.areas.append(area) }
  }
  func addNote(_ note: MissionNote) -> Bool {
    editBrief("Note added: \(note.title)") { $0.notes.insert(note, at: 0) }
  }
  func addObservation(_ observation: MissionObservation) -> Bool {
    editBrief("Object observation added: \(observation.title)") {
      $0.observations.insert(observation, at: 0)
    }
  }
  @discardableResult func reviewObservation(_ id: UUID, state: MissionReviewState, note: String)
    -> Bool
  {
    editBrief("Object review: \(state.rawValue)") {
      guard let i = $0.observations.firstIndex(where: { $0.id == id }) else { return }
      $0.observations[i].state = state
      $0.observations[i].note = note
    }
  }
  func addTask(_ task: MissionTask) -> Bool {
    editBrief("Task added: \(task.title)") { $0.tasks.append(task) }
  }
  func setTask(_ id: UUID, done: Bool) {
    _ = editBrief(done ? "Task completed" : "Task reopened") {
      if let i = $0.tasks.firstIndex(where: { $0.id == id }) { $0.tasks[i].done = done }
    }
  }
  func addMonitor(_ monitor: MissionMonitor) -> Bool {
    editBrief("Automation created: \(monitor.kind.rawValue)") { $0.monitors.append(monitor) }
  }
  func setMonitor(_ id: UUID, enabled: Bool) {
    _ = editBrief(enabled ? "Automation enabled" : "Automation paused") {
      if let i = $0.monitors.firstIndex(where: { $0.id == id }) { $0.monitors[i].enabled = enabled }
    }
  }
  func commitMonitor(missionID: UUID, monitor: MissionMonitor, event: String? = nil) -> Bool {
    guard let i = plans.firstIndex(where: { $0.id == missionID }),
      let j = plans[i].brief?.monitors.firstIndex(where: { $0.id == monitor.id })
    else { return false }
    var next = plans
    // Keep a pause applied while the request was in flight.
    var updated = monitor
    updated.enabled = next[i].brief!.monitors[j].enabled
    next[i].brief!.monitors[j] = updated
    if let event { next[i].brief!.record(event) }
    next[i].updatedAt = Date()
    do {
      try MissionDocuments.save(next, to: Self.file)
      plans = next
      return true
    } catch {
      self.error = "Automation results could not be saved; the previous checkpoint was retained."
      return false
    }
  }
}

struct MissionCatalogMatch: Identifiable {
  var id: String
  var title: String
  var acquiredAt: String
}

@MainActor final class MissionAutomationRunner: ObservableObject {
  @Published private(set) var running: Set<UUID> = []
  @Published private(set) var matches: [UUID: [MissionCatalogMatch]] = [:]
  private weak var model: WorkspaceModel?
  private var timer: Task<Void, Never>?
  private var work: [UUID: Task<Void, Never>] = [:]
  func start(model: WorkspaceModel) {
    guard timer == nil else { return }
    self.model = model
    timer = Task { [weak self] in
      while !Task.isCancelled {
        self?.tick()
        try? await Task.sleep(for: .seconds(60))
      }
    }
  }
  func stop() {
    timer?.cancel()
    timer = nil
    for task in work.values { task.cancel() }
    work.removeAll()
    running.removeAll()
  }
  func tick() {
    guard let model else { return }
    // One overdue run per monitor; never replay every missed interval after sleep/restart.
    for plan in model.missions.plans {
      for monitor in plan.brief?.monitors ?? [] where monitor.isDue(at: Date()) {
        if running.count >= 2 { return }
        run(missionID: plan.id, monitorID: monitor.id)
      }
    }
  }
  func run(missionID: UUID, monitorID: UUID) {
    guard let model, !running.contains(monitorID), running.count < 2,
      let plan = model.missions.plans.first(where: { $0.id == missionID }),
      var monitor = plan.brief?.monitors.first(where: { $0.id == monitorID })
    else { return }
    monitor.lastAttempt = Date()
    guard model.missions.commitMonitor(missionID: missionID, monitor: monitor) else { return }
    let region = plan.brief?.areas.first(where: { $0.id == monitor.areaID })?.region
    running.insert(monitorID)
    work[monitorID] = Task { [weak self, weak model] in
      guard let self, let model else { return }
      defer {
        running.remove(monitorID)
        work[monitorID] = nil
      }
      do {
        let now = Date()
        let start = now.addingTimeInterval(-Double(monitor.lookbackDays) * 86400)
        var found: [MissionCatalogMatch] = []
        var limited = false
        switch monitor.kind {
        case .evidence:
          let missing = plan.items.filter { !model.missionEvidenceAvailable($0) }
          monitor.summary =
            "\(plan.items.count - missing.count)/\(plan.items.count) linked items available locally"
          if !missing.isEmpty {
            monitor.summary +=
              " · missing: " + missing.prefix(4).map(\.title).joined(separator: ", ")
          }
          monitor.summary = String(monitor.summary.prefix(1800))
          monitor.lastSuccess = now
          monitor.failure = nil
        case .sentinel2:
          guard let region else { throw SentinelError.region }
          let query = try SentinelOpticalQuery(
            region: region, start: start, end: now, maximumCloud: monitor.maximumCloud)
          let scenes = try await SentinelOpticalCatalog().search(query)
          found = scenes.map {
            MissionCatalogMatch(id: $0.id, title: $0.id, acquiredAt: $0.acquiredAt)
          }
          limited = scenes.count == 40
          _ = monitor.receive(ids: found.map(\.id), at: now, limited: limited)
        case .sentinel1:
          guard let region else { throw SentinelError.region }
          let query = try SentinelQuery(
            region: region, start: start, end: now, polarization: .both, mode: "IW")
          let page = try await SentinelCatalog().search(query)
          found = page.scenes.map {
            MissionCatalogMatch(id: $0.id.uuidString, title: $0.name, acquiredAt: $0.acquisition)
          }
          limited = page.hasMore
          _ = monitor.receive(ids: found.map(\.id), at: now, limited: limited)
        }
        try Task.checkCancellation()
        // An edited area starts a fresh baseline; discard results for the old geometry.
        if monitor.kind != .evidence {
          let currentRegion = model.missions.plans.first(where: { $0.id == missionID })?
            .brief?.areas.first(where: { $0.id == monitor.areaID })?.region
          guard currentRegion == region else { return }
        }
        if model.missions.commitMonitor(
          missionID: missionID, monitor: monitor,
          event: "\(monitor.kind.rawValue): \(monitor.summary)")
        {
          matches[monitorID] = found
        }
      } catch is CancellationError {} catch {
        guard !Task.isCancelled else { return }
        if monitor.kind != .evidence {
          let currentRegion = model.missions.plans.first(where: { $0.id == missionID })?
            .brief?.areas.first(where: { $0.id == monitor.areaID })?.region
          guard currentRegion == region else { return }
        }
        monitor.failure =
          "Check failed. Check network connectivity and catalogue availability, then retry. Previous successful results remain retained."
        _ = model.missions.commitMonitor(
          missionID: missionID, monitor: monitor,
          event: "Automation failed: \(monitor.kind.rawValue)")
      }
    }
  }
}

extension WorkspaceModel {
  func missionEvidenceAvailable(_ item: MissionItem) -> Bool {
    switch item.kind {
    case .optical:
      guard let image = optical.images.first(where: { $0.id.uuidString == item.referenceID }) else {
        return false
      }
      return FileManager.default.fileExists(
        atPath: image.directory.appendingPathComponent("preview.png").path)
    case .radar:
      guard let job = sar.jobs.first(where: { $0.id == item.referenceID }),
        job.result.state == .completed
      else { return false }
      return !job.result.outputs.isEmpty
        && job.result.outputs.allSatisfy {
          guard let file = $0.file($0.preview, in: job.directory) else { return false }
          return FileManager.default.fileExists(atPath: file.path)
        }
    default: return item.isValid
    }
  }
  func showMissionArea(_ area: MissionArea) {
    missions.comparisonEnabled = false
    setSarSearchRegion(area.region)
    focusSatelliteBounds([area.region.west, area.region.south, area.region.east, area.region.north])
    revealMap?()
  }
  func showObservation(_ observation: MissionObservation) {
    if let id = observation.evidenceID,
      let item = missions.plan?.items.first(where: { $0.id == id })
    {
      openMissionItem(item)
    }
    if let b = observation.bounds {
      reviewOutline = [(b[1], b[0]), (b[1], b[2]), (b[3], b[2]), (b[3], b[0]), (b[1], b[0])]
        .compactMap { Coordinate(latitude: $0.0, longitude: $0.1) }
    } else {
      reviewOutline = []
    }
    fly(to: observation.coordinate, altitude: 8000)
    missionFocus = GeoRecord(
      id: "mission:\(observation.id)", feed: .annotations, label: observation.title,
      coordinate: observation.coordinate, altitude: nil, observedAt: observation.observedAt,
      source: observation.method,
      details: [
        "Review": observation.state.rawValue, "Category": observation.category,
        "Note": observation.note,
      ], footprint: reviewOutline)
    selectedID = missionFocus?.id
    revealMap?()
  }
  func openMonitorResults(_ monitor: MissionMonitor) {
    guard let area = missions.brief.areas.first(where: { $0.id == monitor.areaID }) else { return }
    showMissionArea(area)
    if monitor.kind == .sentinel2 {
      optical.source = .sentinel2
      optical.startDate = Date().addingTimeInterval(-Double(monitor.lookbackDays) * 86400)
      optical.endDate = Date()
      optical.maximumCloud = monitor.maximumCloud
      satellite.page = "Reference imagery"
      optical.search(region: area.region)
    } else {
      sarAcquisition.startDate = Date().addingTimeInterval(-Double(monitor.lookbackDays) * 86400)
      sarAcquisition.endDate = Date()
      sarAcquisition.polarization = .both
      sarAcquisition.mode = "IW"
      satellite.page = "SAR workflow"
      sar.workflow = "Find & download"
      sarAcquisition.search(region: area.region)
    }
    openPanel(.satellite)
  }
}

extension MissionWorkspace {
  @discardableResult func updateArea(_ area: MissionArea) -> Bool {
    editBrief("Area updated: \(area.name)") { brief in
      guard let i = brief.areas.firstIndex(where: { $0.id == area.id }) else { return }
      if brief.areas[i].region != area.region {
        for j in brief.monitors.indices where brief.monitors[j].areaID == area.id {
          brief.monitors[j].lastAttempt = nil
          brief.monitors[j].lastSuccess = nil
          brief.monitors[j].seenIDs = []
          brief.monitors[j].summary = "Area changed; the next check establishes a new baseline."
          brief.monitors[j].failure = nil
        }
      }
      brief.areas[i] = area
    }
  }
  @discardableResult func updateNote(_ note: MissionNote) -> Bool {
    editBrief("Note updated: \(note.title)") { brief in
      guard let i = brief.notes.firstIndex(where: { $0.id == note.id }) else { return }
      brief.notes[i] = note
    }
  }
  func removeArea(_ id: UUID) {
    let alert = NSAlert()
    alert.messageText = "Remove this mission area?"
    alert.informativeText =
      "Notes, observations and source files are kept. Automations attached to this area are removed."
    alert.addButton(withTitle: "Cancel")
    alert.addButton(withTitle: "Remove area")
    guard alert.runModal() == .alertSecondButtonReturn else { return }
    _ = editBrief("Mission area removed; source evidence retained") { brief in
      brief.removeMonitoringArea(id)
      brief.areas.removeAll { $0.id == id }
      brief.monitors.removeAll { $0.areaID == id }
      for i in brief.notes.indices where brief.notes[i].areaID == id { brief.notes[i].areaID = nil }
      for i in brief.observations.indices where brief.observations[i].areaID == id {
        brief.observations[i].areaID = nil
      }
      for i in brief.tasks.indices where brief.tasks[i].areaID == id { brief.tasks[i].areaID = nil }
    }
  }
}

/// Removing a review entry never deletes its source imagery or catalogue data.
enum MissionEntryKind: String { case note, task, observation, automation }
extension MissionWorkspace {
  @discardableResult func removeEntry(_ kind: MissionEntryKind, id: UUID) -> Bool {
    let alert = NSAlert()
    alert.messageText = "Delete this \(kind.rawValue)?"
    alert.informativeText =
      "This removes the saved mission entry. Source imagery and other mission entries are kept."
    alert.addButton(withTitle: "Cancel")
    alert.addButton(withTitle: "Delete")
    guard alert.runModal() == .alertSecondButtonReturn else { return false }
    let saved = editBrief("Deleted \(kind.rawValue)") { brief in
      switch kind {
      case .note: brief.notes.removeAll { $0.id == id }
      case .task: brief.tasks.removeAll { $0.id == id }
      case .observation:
        brief.observations.removeAll { $0.id == id }
        brief.removeObservationReferences(id)
      case .automation:
        brief.removeMonitoringCheck(id)
        brief.monitors.removeAll { $0.id == id }
      }
    }
    if saved, kind == .observation { reviewDrafts[id] = nil }
    return saved
  }
}
