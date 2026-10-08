import AppKit
import GodEyeCore
import SwiftUI

@MainActor final class MissionWorkspace: ObservableObject {
  @Published var plans: [MissionPlan] = []
  @Published var selectedID: UUID?
  @Published var name = ""
  @Published var objective = ""
  @Published var notes = ""
  @Published var itemName = ""
  @Published var error: String?
  @Published var status = "Create a mission to organize locations and evidence."
  @Published var comparisonEnabled = false
  @Published var baseID = ""
  @Published var topID = ""
  @Published var blend = 0.5
  static var file: URL { SatelliteStorage.root.appendingPathComponent("Missions/plans.json") }
  var plan: MissionPlan? { plans.first { $0.id == selectedID } }
  private var comparisonDraft: MissionComparison? {
    guard !baseID.isEmpty || !topID.isEmpty else { return nil }
    return MissionComparison(baseID: baseID, topID: topID, blend: blend)
  }
  var dirty: Bool {
    guard let plan else { return false }
    return name != plan.name || objective != plan.objective || notes != plan.notes
      || comparisonDraft != plan.comparison
  }
  init() {
    do {
      plans = try MissionDocuments.load(from: Self.file)
      setSelection(plans.first?.id)
    } catch { self.error = "Saved missions could not be read. The existing library was preserved." }
  }
  private func setSelection(_ id: UUID?) {
    selectedID = id
    name = plan?.name ?? ""
    objective = plan?.objective ?? ""
    notes = plan?.notes ?? ""
    baseID = plan?.comparison?.baseID ?? ""
    topID = plan?.comparison?.topID ?? ""
    blend = plan?.comparison?.blend ?? 0.5
    comparisonEnabled = false
  }
  func resolveUnsavedChanges() -> Bool {
    guard dirty else { return true }
    let alert = NSAlert()
    alert.messageText = "Save changes to this mission?"
    alert.addButton(withTitle: "Save")
    alert.addButton(withTitle: "Discard changes")
    alert.addButton(withTitle: "Cancel")
    switch alert.runModal() {
    case .alertFirstButtonReturn: return save()
    case .alertSecondButtonReturn:
      setSelection(selectedID)
      return true
    default: return false
    }
  }
  func select(_ id: UUID?) {
    guard id != selectedID, resolveUnsavedChanges() else { return }
    setSelection(id)
  }
  func create() {
    if dirty, !save() { return }
    let alert = NSAlert()
    alert.messageText = "New mission"
    alert.informativeText = "Name this investigation or planning workspace."
    let field = NSTextField(frame: NSRect(x: 0, y: 0, width: 320, height: 24))
    field.placeholderString = "Mission name"
    alert.accessoryView = field
    alert.addButton(withTitle: "Create")
    alert.addButton(withTitle: "Cancel")
    alert.window.initialFirstResponder = field
    guard alert.runModal() == .alertFirstButtonReturn else { return }
    let plan = MissionPlan(name: field.stringValue)
    guard plan.isValid else {
      error = "Enter a mission name of 1–120 characters."
      return
    }
    do {
      let next = plans + [plan]
      try MissionDocuments.save(next, to: Self.file)
      plans = next
      setSelection(plan.id)
      error = nil
      status = "Mission created locally."
    } catch { self.error = "The mission could not be created." }
  }
  @discardableResult func save() -> Bool {
    guard let index = plans.firstIndex(where: { $0.id == selectedID }) else { return false }
    var next = plans
    next[index].name = name.trimmingCharacters(in: .whitespacesAndNewlines)
    next[index].objective = objective
    next[index].notes = notes
    next[index].comparison = comparisonDraft
    next[index].updatedAt = Date()
    guard next[index].isValid else {
      error = "Check name and note length limits."
      return false
    }
    do {
      try MissionDocuments.save(next, to: Self.file)
      plans = next
      name = next[index].name
      error = nil
      status = "Mission saved locally."
      return true
    } catch {
      self.error = "Mission save failed. Your draft is still available."
      return false
    }
  }
  func add(_ item: MissionItem) {
    guard item.isValid else {
      error = "This record exceeds the supported snapshot limits or contains invalid metadata."
      return
    }
    guard save(), let index = plans.firstIndex(where: { $0.id == selectedID }) else { return }
    if plans[index].items.contains(where: {
      $0.kind == item.kind && $0.referenceID == item.referenceID
    }) {
      status = "This evidence is already linked to the mission."
      return
    }
    var next = plans
    next[index].items.append(item)
    next[index].updatedAt = Date()
    do {
      try MissionDocuments.save(next, to: Self.file)
      plans = next
      status = "Added \(item.title)."
      error = nil
      itemName = ""
    } catch { self.error = "The evidence link could not be saved." }
  }
  func remove(_ id: UUID) {
    guard save(), let index = plans.firstIndex(where: { $0.id == selectedID }) else { return }
    var next = plans
    next[index].items.removeAll { $0.id == id }
    next[index].updatedAt = Date()
    do {
      try MissionDocuments.save(next, to: Self.file)
      plans = next
      error = nil
    } catch { self.error = "The evidence link could not be removed." }
  }
  func export() {
    guard save(), let plan else { return }
    let panel = NSSavePanel()
    panel.nameFieldStringValue = "\(plan.name)-mission.json"
    guard panel.runModal() == .OK, let url = panel.url else { return }
    do { try SatelliteDocuments.save(plan, to: url, dateEncodingStrategy: .deferredToDate) } catch {
      self.error = "The mission could not be exported."
    }
  }
}

struct RasterChoice: Identifiable {
  var id: String
  var title: String
  var overlay: SarOverlay
}

extension WorkspaceModel {
  var rasterChoices: [RasterChoice] {
    let opticalChoices = optical.images.compactMap { image -> RasterChoice? in
      guard let bounds = image.result.bounds else { return nil }
      return RasterChoice(
        id: "optical:\(image.id.uuidString)", title: image.title,
        overlay: SarOverlay(
          preview: image.directory.appendingPathComponent("preview.png"), bounds: bounds,
          label: image.title, pixelGrid: optical.pixelGrid, opacity: 1))
    }
    let radarChoices = sar.jobs.filter { $0.result.state == .completed }.flatMap { job in
      job.result.outputs.compactMap { output -> RasterChoice? in
        guard output.hasValidDisplay, output.hasValidBounds,
          let preview = output.file(output.preview, in: job.directory)
        else { return nil }
        return RasterChoice(
          id: "radar:\(job.id):\(output.id)",
          title: "SAR \(output.id) · \(job.result.metadata?.acquisitionStart ?? "")",
          overlay: SarOverlay(
            preview: preview, bounds: output.bounds,
            label: "SAR \(output.id) · \(job.result.metadata?.acquisitionStart ?? "")", opacity: 1))
      }
    }
    return opticalChoices + radarChoices
  }
  var comparisonLayers: [SarOverlay] {
    guard missions.comparisonEnabled else { return [] }
    guard let base = rasterChoices.first(where: { $0.id == missions.baseID }),
      let top = rasterChoices.first(where: { $0.id == missions.topID }), base.id != top.id
    else { return [] }
    var upper = top.overlay
    upper.opacity = Float(missions.blend)
    return [base.overlay, upper]
  }
  func addMissionView() {
    var item = MissionItem(
      kind: .view, referenceID: UUID().uuidString,
      title: missions.itemName.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty
        ? String(format: "View %.5f°, %.5f°", camera.center.latitude, camera.center.longitude)
        : missions.itemName)
    item.camera = camera
    missions.add(item)
  }
  func addMissionRecord() {
    guard let selected else { return }
    var item = MissionItem(kind: .record, referenceID: selected.id, title: selected.label)
    item.record = MissionRecord(selected)
    missions.add(item)
  }
  func addMissionSite() {
    guard let site = satellite.site else { return }
    var item = MissionItem(kind: .site, referenceID: site.id.uuidString, title: site.name)
    item.site = site
    missions.add(item)
  }
  func addMissionOptical() {
    guard let image = optical.image else { return }
    missions.add(MissionItem(kind: .optical, referenceID: image.id.uuidString, title: image.title))
  }
  func addMissionRadar() {
    guard let job = sar.currentJob else { return }
    missions.add(
      MissionItem(
        kind: .radar, referenceID: job.id,
        title: "SAR · \(job.result.metadata?.acquisitionStart ?? job.id)"))
  }
  func openMissionItem(_ item: MissionItem) {
    missions.comparisonEnabled = false
    switch item.kind {
    case .view:
      if let saved = item.camera {
        camera = saved
        enforceSurfaceClearance()
      }
    case .record:
      if let record = item.record {
        fly(to: record.coordinate, altitude: 20_000)
        if !enabled.contains(record.feed) { setEnabled(record.feed, true) }
        selectedID = record.id
      }
    case .site:
      if let site = item.site {
        satellite.siteID = satellite.sites.first(where: { $0.id == site.id })?.id
        setSarSearchRegion(site.region)
        focusSatelliteBounds([
          site.region.west, site.region.south, site.region.east, site.region.north,
        ])
      }
    case .annotation:
      if let point = item.annotation?.points.first { fly(to: point, altitude: 20_000) }
    case .optical:
      guard let image = optical.images.first(where: { $0.id.uuidString == item.referenceID }) else {
        missions.error = "This image was removed from the local library."
        return
      }
      optical.source = image.result.scene?.sensor ?? .sentinel2
      optical.imageID = image.id
      optical.showOverlay = true
      rasterMode = "Optical"
      if let bounds = image.result.bounds { focusSatelliteBounds(bounds) }
    case .radar:
      guard let job = sar.jobs.first(where: { $0.id == item.referenceID }) else {
        missions.error = "This radar acquisition is no longer in the local library."
        return
      }
      sar.selectedJob = job.id
      sar.selectedOutput = job.result.outputs.first?.id ?? "VV"
      sar.overlayEnabled = true
      rasterMode = "SAR"
      if let bounds = sar.overlay?.bounds { focusSatelliteBounds(bounds) }
    }
  }
}
