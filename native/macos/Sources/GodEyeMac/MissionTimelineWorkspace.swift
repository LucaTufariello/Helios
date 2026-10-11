import AppKit
import GodEyeCore

extension WorkspaceModel {
  func acquisitionSnapshot(_ item: MissionItem) -> MissionAcquisition? {
    if let saved = item.acquisition { return saved }
    if item.kind == .optical,
      let result = optical.images.first(where: { $0.id.uuidString == item.referenceID })?.result,
      let scene = result.scene
    {
      return MissionAcquisition(
        sourceID: scene.id, sensor: scene.platform,
        acquiredAt: ProviderParsers.date(scene.acquiredAt), bounds: result.bounds,
        displayBand: result.mode ?? "RGB", resolutionMetres: result.sourceResolutionMetres?.first)
    }
    if item.kind == .radar, let job = sar.jobs.first(where: { $0.id == item.referenceID }),
      let product = job.result.metadata
    {
      let output = job.result.outputs.first(where: { $0.id == "VV" }) ?? job.result.outputs.first
      // Source identity is acquisition-based; reprocessing the same SAFE is not another observation.
      let key =
        "\(product.satellite)|\(product.acquisitionStart)|\(product.mode)|\(product.relativeOrbit.map(String.init) ?? "unknown")"
      return MissionAcquisition(
        sourceID: key, sensor: product.satellite,
        acquiredAt: ProviderParsers.date(product.acquisitionStart), bounds: output?.bounds,
        displayBand: output?.id, resolutionMetres: job.result.options?.pixelSpacing)
    }
    return nil
  }
  func captureMissionAcquisitions() {
    guard let plan = missions.plan else { return }
    let metadata = Dictionary(
      uniqueKeysWithValues: plan.items.compactMap { item -> (UUID, MissionAcquisition)? in
        guard item.acquisition == nil, let value = acquisitionSnapshot(item), value.isValid else {
          return nil
        }
        return (item.id, value)
      })
    guard !metadata.isEmpty else {
      missions.status = "Acquisition metadata is already saved or unavailable."
      return
    }
    guard !missions.dirty || missions.save(),
      let index = missions.plans.firstIndex(where: { $0.id == plan.id })
    else { return }
    var next = missions.plans
    for i in next[index].items.indices {
      if let value = metadata[next[index].items[i].id] { next[index].items[i].acquisition = value }
    }
    if next[index].brief == nil { next[index].brief = MissionBrief() }
    next[index].brief?.record("Saved acquisition metadata for \(metadata.count) evidence links")
    next[index].updatedAt = Date()
    do {
      try MissionDocuments.save(next, to: MissionWorkspace.file)
      missions.plans = next
      missions.status = "Acquisition times and footprints saved with the mission."
      missions.error = nil
    } catch {
      missions.error =
        "Acquisition metadata could not be saved. The previous mission was preserved."
    }
  }
  func missionTimelineEntries() -> [MissionTimelineEntry] {
    guard let plan = missions.plan else { return [] }
    var entries = plan.items.compactMap { item -> MissionTimelineEntry? in
      if item.kind == .optical || item.kind == .radar {
        let acquisition = acquisitionSnapshot(item)
        let rasterID =
          item.kind == .optical
          ? "optical:\(item.referenceID)"
          : acquisition?.displayBand.map { "radar:\(item.referenceID):\($0)" }
        return MissionTimelineEntry(
          id: item.id, kind: item.kind == .optical ? .optical : .radar,
          title: item.title, time: acquisition?.acquiredAt,
          source: acquisition?.sensor ?? "Unknown sensor",
          sourceID: acquisition?.sourceID, bounds: acquisition?.bounds, evidenceID: item.id,
          available: missionEvidenceAvailable(item), rasterID: rasterID,
          displayBand: acquisition?.displayBand, resolutionMetres: acquisition?.resolutionMetres,
          detail: item.acquisition == nil
            ? "Metadata read from local library; use Save acquisition metadata to retain it if the raster is later removed."
            : "Acquisition metadata retained in mission.")
      }
      if let record = item.record, item.kind == .record {
        return MissionTimelineEntry(
          id: item.id, kind: .record, title: item.title, time: record.observedAt,
          source: record.source, sourceID: record.id, coordinate: record.coordinate,
          evidenceID: item.id,
          detail:
            "Saved source snapshot; subsequent live-feed updates do not change its observation time."
        )
      }
      return nil  // Views/sites/annotations are context, not remotely sensed observations.
    }
    let evidence = Dictionary(uniqueKeysWithValues: entries.map { ($0.id, $0) })
    for object in plan.brief?.observations ?? [] {
      entries.append(
        MissionTimelineEntry(
          id: object.id, kind: .object, title: object.title,
          time: object.observedAt, source: object.method, bounds: object.bounds,
          coordinate: object.coordinate,
          areaIDs: object.areaID.map { [$0] } ?? [], evidenceID: object.evidenceID,
          detail: "\(object.state.rawValue) · \(object.category) · \(object.note)"))
    }
    for note in plan.brief?.notes ?? [] {
      let parent = note.evidenceID.flatMap { evidence[$0] }
      entries.append(
        MissionTimelineEntry(
          id: note.id, kind: .note, title: note.title,
          time: note.createdAt, source: "Analyst note", bounds: parent?.bounds,
          coordinate: parent?.coordinate,
          areaIDs: note.areaID.map { [$0] } ?? [], evidenceID: note.evidenceID,
          detail: note.body + (note.reference.isEmpty ? "" : " · " + note.reference)))
    }
    return entries
  }
  @discardableResult func compareMissionTimeline(before: UUID?, after: UUID?) -> Bool {
    // Recheck current local availability; a cached timeline must not display a removed raster.
    let entries = missionTimelineEntries()
    let a = entries.first { $0.id == before }
    let b = entries.first { $0.id == after }
    if let issue = MissionTimeline.comparisonProblem(a, b) {
      missions.error = issue
      return false
    }
    guard let a, let b, let base = a.rasterID, let top = b.rasterID,
      rasterChoices.contains(where: { $0.id == base }),
      rasterChoices.contains(where: { $0.id == top })
    else {
      missions.error = "A selected raster is unavailable."
      return false
    }
    missions.baseID = base
    missions.topID = top
    missions.blend = 0.5
    missions.comparisonEnabled = true
    if let bounds = a.bounds { focusSatelliteBounds(bounds) }
    missions.error = nil
    revealMap?()
    return true
  }
  func showTimelineEntry(_ entry: MissionTimelineEntry) {
    if entry.kind == .object,
      let object = missions.brief.observations.first(where: { $0.id == entry.id })
    {
      showObservation(object)
      return
    }
    if let id = entry.evidenceID, let item = missions.plan?.items.first(where: { $0.id == id }) {
      openMissionItem(item)
      revealMap?()
      return
    }
    if let area = missions.brief.areas.first(where: { entry.areaIDs.contains($0.id) }) {
      showMissionArea(area)
    }
  }
}
