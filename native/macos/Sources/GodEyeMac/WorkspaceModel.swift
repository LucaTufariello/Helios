import AppKit
import GodEyeCore
import SwiftUI

@MainActor final class WorkspaceModel: ObservableObject {
  let sar = SarWorkspace()
  let sarAcquisition = SarAcquisitionWorkspace()
  let satellite = SatelliteWorkspace()
  let optical = OpticalWorkspace()
  let missions = MissionWorkspace()
  let missionAutomation = MissionAutomationRunner()
  let missionDetection = MissionDetection()
  var revealMap: (() -> Void)?
  var revealMission: (() -> Void)?
  @Published var missionFocus: GeoRecord? { didSet { rebuildRecords() } }
  @Published var missionPick = false
  @Published var missionPickedCoordinate: Coordinate?
  @Published var missionAreaCapture = false
  @Published var workspaceSection = "Map" {
    didSet {
      guard workspaceSection != oldValue else { return }
      sourceQuery = ""
      panels.removeAll { [.sources, .satellite, .mission].contains($0) }
      switch workspaceSection {
      case "Satellite": openPanel(.satellite)
      case "Mission planner": revealMission?()
      case "Marine", "Sky", "Ground": openPanel(.sources)
      default:
        panels.removeAll()
        showTools = false
      }
    }
  }
  func showSection(_ section: String) {
    if workspaceSection != section {
      workspaceSection = section
      return
    }
    switch section {
    case "Satellite": openPanel(.satellite)
    case "Mission planner": revealMission?()
    case "Map":
      panels.removeAll()
      showTools = false
    default: openPanel(.sources)
    }
  }
  @Published var panels: [WorkspacePanel] = []
  @Published var rasterMode = "None"
  @Published var rawScope = "Selected record"
  func openPanel(_ panel: WorkspacePanel) {
    if panel == .mission {
      revealMission?()
      return
    }
    panels.removeAll { $0 == panel }
    panels.append(panel)
  }
  func closePanel(_ panel: WorkspacePanel) {
    panels.removeAll { $0 == panel }
    if panel == .tools { showTools = false }
  }
  var sectionSources: [FeedKind] {
    switch workspaceSection {
    case "Marine": [.vessels, .cables]
    case "Sky": [.flights, .military, .satellites, .launches, .atmosphere]
    case "Ground":
      [
        .installations, .energy, .datacenters, .dams, .transit, .traffic, .cameras, .earthquakes,
        .disasters, .fires, .annotations,
      ]
    default: FeedKind.allCases
    }
  }
  @Published var reviewOutline: [Coordinate] = []
  func focusSatelliteBounds(_ b: [Double]) {
    guard ChangeReviewResult.validBounds(b),
      let center = Coordinate(latitude: (b[1] + b[3]) / 2, longitude: (b[0] + b[2]) / 2)
    else { return }
    fly(to: center, altitude: max(8000, max(b[2] - b[0], b[3] - b[1]) * 160_000))
  }
  @Published var sarSearchRegion: SentinelRegion?
  @Published var sarRegionSelecting = false
  @Published var sarRegionFirstCorner: Coordinate?
  @Published var sarSceneFootprints: [[Coordinate]] = []
  func beginSarRegionSelection() {
    missionPick = false
    missionAreaCapture = false
    drawing = false
    routeEndpoint = nil
    sarRegionFirstCorner = nil
    sarAcquisition.error = nil
    sarRegionSelecting = true
    workspaceSection = "Satellite"
    showTools = false
    closePanel(.satellite)
  }
  func cancelSarRegionSelection() {
    missionAreaCapture = false
    sarRegionSelecting = false
    sarRegionFirstCorner = nil
  }
  func setSarSearchRegion(_ region: SentinelRegion) {
    sarSearchRegion = region
    sarSceneFootprints = []
    sarAcquisition.resetSearch()
    optical.resetSearch()
  }
  func setSarRegionToView() {
    do {
      let bounds = try GeoBounds.viewport(camera: camera, aspect: viewportAspect, maximumSpan: 10)
      setSarSearchRegion(
        try SentinelRegion(
          west: bounds.west, south: bounds.south, east: bounds.east, north: bounds.north))
    } catch { sarAcquisition.error = error.localizedDescription }
  }
  func focusSarScene(_ scene: SentinelScene) {
    sarSceneFootprints = scene.footprint
    let points = scene.footprint.flatMap { $0 }
    if let west = points.map(\.longitude).min(), let east = points.map(\.longitude).max(),
      let south = points.map(\.latitude).min(), let north = points.map(\.latitude).max(),
      east - west < 180,
      let center = Coordinate(latitude: (south + north) / 2, longitude: (west + east) / 2)
    {
      fly(to: center, altitude: max(20_000, max(east - west, north - south) * 160_000))
    }
    showTools = false
  }
  @Published var sarOverlayStatus: String?
  @Published var basemap = Basemap.satellite
  @Published var displayStyle = DisplayStyle.standard
  @Published var terrainEnabled = true {
    didSet { if terrainEnabled != oldValue { surfaceSelection = nil } }
  }
  @Published var terrainStatus = "Terrain: zoom below 200 km to load elevation"
  @Published var tilesetURL: URL?
  @Published var sceneStatus = "No 3D tileset selected"
  @Published var sceneCredits = ""
  @Published var surfaceSelection: SurfacePoint?
  @Published var surfaceEvidence = ""
  @Published var surfaceError: String?
  func orient(heading: Double, tilt: Double) {
    trackedID = nil
    routeFlight?.cancel()
    camera.orient(heading: heading, tilt: tilt)
    enforceSurfaceClearance()
  }
  func enforceSurfaceClearance() {
    guard let renderer else { return }
    var adjusted = camera
    adjusted.ensureClearance(targetHeight: renderer.surfaceHeight(at: camera.center) ?? 0) {
      renderer.surfaceHeight(at: $0)
    }
    if adjusted != camera { camera = adjusted }
  }
  func loadTileset(_ url: URL) {
    guard
      url.isFileURL
        || (url.scheme == "https" && url.host != nil && url.user == nil && url.password == nil)
    else {
      surfaceError = "Choose a local tileset.json or an HTTPS URL."
      return
    }
    surfaceError = nil
    tilesetURL = url
    sceneCredits = ""
    sceneStatus = "Loading 3D tileset…"
    surfaceSelection = nil
  }
  func openTileset() {
    let panel = NSOpenPanel()
    panel.allowedContentTypes = [.json]
    panel.canChooseDirectories = false
    panel.allowsMultipleSelection = false
    panel.message =
      "Select tileset.json. Referenced files must be in the same directory or its subdirectories."
    if panel.runModal() == .OK, let url = panel.url { loadTileset(url) }
  }
  func focusTileset() {
    guard let camera = renderer?.focusScene() else {
      surfaceError = "The tileset bounds are not available yet."
      return
    }
    surfaceError = nil
    trackedID = nil
    routeFlight?.cancel()
    self.camera = camera
    enforceSurfaceClearance()
  }
  func clearTileset() {
    tilesetURL = nil
    sceneStatus = "No 3D tileset selected"
    sceneCredits = ""
    surfaceSelection = nil
    surfaceError = nil
  }
  @Published var sourceConfiguration = SourceConfiguration() {
    didSet { restartConfiguredSources() }
  }
  @Published var cameraRecord: GeoRecord?
  @Published var showTools = false
  @Published var toolTab = "Directions"
  @Published var trackedID: String?
  @Published var estimatedOrbit: ScenePath?
  @Published var orbitError: String?
  @Published var orbitDate: Date?
  private var orbitTask: Task<Void, Never>?
  private let traffic = TrafficSource()
  @Published var showTrails = true
  @Published var playbackTime: Date? {
    didSet {
      if playbackTime != nil { trackedID = nil }
      rebuildRecords()
    }
  }
  @Published var annotations: [LocalAnnotation] = []
  @Published var storageError: String?
  @Published var route: NativeRoute?
  @Published var routeError: String?
  @Published var routeLoading = false
  @Published var routeA: Coordinate?
  @Published var routeB: Coordinate?
  @Published var travelMode = TravelMode.car
  @Published var drawingTitle = ""
  @Published var drawingPoints: [Coordinate] = []
  @Published var drawing = false
  @Published var closeDrawing = false
  @Published var routeEndpoint: String?
  private var history = TrackHistory()
  private let nativeSources = NativeSources()
  private let ais = AISStream()
  private var routeTask: Task<Void, Never>?
  private var routeGeneration = UUID()
  private var routeFlight: Task<Void, Never>?
  init() {
    sourceConfiguration.firmsKey = ProviderKeys.read("firms")
    do { annotations = try AnnotationStorage.load() } catch {
      storageError =
        "Saved annotations could not be loaded. The existing file has been left intact."
    }
  }
  @Published var dailyImagery = DailyImagery.off
  @Published var imageryOpacity = 0.85
  @Published var camera = Camera.initial { didSet { scheduleLocalRefresh() } }
  @Published var viewportAspect = 1.0 {
    didSet { if abs(viewportAspect - oldValue) > 0.01 { scheduleLocalRefresh() } }
  }
  @Published var solarEnabled = false { didSet { updateSolar() } }
  @Published private(set) var solar: SolarState?
  private let additionalSources = AdditionalSources()
  private var localContexts: [FeedKind: String] = [:]
  private var pendingSelection: (String, FeedKind)?
  private var pendingLocalFeeds: Set<FeedKind> = []
  private var localTask: Task<Void, Never>?
  private var solarTask: Task<Void, Never>?
  @Published var enabled: Set<FeedKind> = []
  @Published var snapshots: [FeedKind: Snapshot] = [:]
  @Published var errors: [FeedKind: String] = [:]
  @Published var fetched: [FeedKind: Date] = [:]
  @Published var loading: Set<FeedKind> = []
  @Published var selectedID: String? {
    didSet {
      if selectedID != oldValue {
        orbitTask?.cancel()
        estimatedOrbit = nil
        orbitError = nil
        orbitDate = nil
      }
      if let pendingSelection, selectedID != pendingSelection.0 { self.pendingSelection = nil }
    }
  }
  @Published var sourceQuery = ""
  @Published var recordQuery = "" { didSet { updateFilteredRecords() } }
  @Published private(set) var records: [GeoRecord] = []
  @Published private(set) var filteredRecords: [GeoRecord] = []
  private var recordsByID: [String: GeoRecord] = [:]
  @Published var selectedOnly = false
  @Published var searchQuery = ""
  @Published var searchResults: [(String, Coordinate)] = []
  @Published var searchError: String?
  @Published var searchLoading = false
  @Published var imageryStatus = "Loading dated Esri imagery…"
  @Published var renderError: String?
  @Published var showMigration = false
  var renderer: GlobeRenderer?
  let transport = Transport()
  private var tasks: [FeedKind: Task<Void, Never>] = [:]
  private var generations: [FeedKind: UUID] = [:]
  private var searchTask: Task<Void, Never>?
  private var searchGeneration = UUID()
  private var visible = true

  private func rebuildRecords() {
    let military = Set(
      enabled.contains(.military)
        ? (snapshots[.military]?.records ?? []).compactMap { $0.details["ICAO"] } : [])
    records = FeedKind.allCases.flatMap { feed in
      enabled.contains(feed)
        ? (snapshots[feed]?.records ?? []).filter {
          feed != .flights || !military.contains($0.details["ICAO"] ?? "")
        } : []
    }
    if let playbackTime {
      records = records.compactMap { record in
        [.flights, .military, .vessels, .transit].contains(record.feed)
          ? history.delayed(record, at: playbackTime) : record
      }
    }
    if let missionFocus { records.append(missionFocus) }
    recordsByID = Dictionary(uniqueKeysWithValues: records.map { ($0.id, $0) })
    updateFilteredRecords()
  }
  private func updateFilteredRecords() {
    filteredRecords = records.filter {
      recordQuery.isEmpty || $0.label.localizedCaseInsensitiveContains(recordQuery)
        || $0.id.localizedCaseInsensitiveContains(recordQuery)
        || $0.details.values.contains { $0.localizedCaseInsensitiveContains(recordQuery) }
    }.sorted { $0.label < $1.label }
  }
  var selected: GeoRecord? { selectedID.flatMap { recordsByID[$0] } }
  var sources: [FeedKind] {
    FeedKind.allCases.filter {
      (!selectedOnly || enabled.contains($0))
        && (sourceQuery.isEmpty || $0.name.localizedCaseInsensitiveContains(sourceQuery)
          || $0.credit.localizedCaseInsensitiveContains(sourceQuery))
    }
  }
  func setEnabled(_ feed: FeedKind, _ value: Bool) {
    if value {
      enabled.insert(feed)
      if visible { start(feed) }
    } else {
      let selectedFeed = selected?.feed
      enabled.remove(feed)
      if pendingSelection?.1 == feed { pendingSelection = nil }
      generations[feed] = UUID()
      tasks.removeValue(forKey: feed)?.cancel()
      loading.remove(feed)
      snapshots.removeValue(forKey: feed)
      errors.removeValue(forKey: feed)
      fetched.removeValue(forKey: feed)
      if selectedFeed == feed { selectedID = nil }
      if trackedID.flatMap({ recordsByID[$0]?.feed }) == feed { trackedID = nil }
      if feed == .cameras { cameraRecord = nil }
      if feed == .vessels { Task { await ais.stop() } }
      rebuildRecords()
    }
  }
  private func start(_ feed: FeedKind) {
    if feed == .annotations {
      updateAnnotations()
      return
    }
    localContexts[feed] = localContext(feed)
    pendingLocalFeeds.remove(feed)
    tasks.removeValue(forKey: feed)?.cancel()
    let generation = UUID()
    generations[feed] = generation
    tasks[feed] = Task { [weak self, transport] in
      while !Task.isCancelled {
        guard let self, self.enabled.contains(feed), self.visible,
          self.generations[feed] == generation
        else { return }
        self.loading.insert(feed)
        let camera = self.camera
        let center = camera.center
        let aspect = self.viewportAspect
        do {
          let snapshot: Snapshot
          if [.installations, .energy].contains(feed) {
            snapshot = try await self.additionalSources.snapshot(
              feed: feed, camera: camera, aspect: aspect)
          } else if feed == .traffic {
            snapshot = try await self.traffic.snapshot(
              camera: camera, aspect: aspect, key: ProviderKeys.read("tomtom"))
          } else if feed == .vessels {
            snapshot = try await self.ais.snapshot(key: ProviderKeys.read("ais"))
          } else if [
            .satellites, .launches, .fires, .transit, .cameras, .datacenters, .dams, .cables,
            .sentinel,
          ].contains(feed) {
            snapshot = try await self.nativeSources.snapshot(
              feed, center: center, configuration: self.sourceConfiguration)
          } else {
            snapshot = try await transport.snapshot(feed, center: center)
          }
          try Task.checkCancellation()
          guard self.generations[feed] == generation, self.enabled.contains(feed), self.visible
          else { return }
          self.snapshots[feed] = snapshot
          self.history.ingest(snapshot.records)
          self.rebuildRecords()
          if feed == .satellites, self.estimatedOrbit != nil { self.requestOrbit() }
          if let id = self.trackedID, let record = self.recordsByID[id], record.feed == feed {
            self.camera = Camera(
              center: record.coordinate, altitude: self.camera.altitude,
              heading: self.camera.heading, tilt: self.camera.tilt)
            self.enforceSurfaceClearance()
          }
          if let pending = self.pendingSelection, pending.1 == feed {
            self.selectedID = self.recordsByID[pending.0] == nil ? nil : pending.0
            self.pendingSelection = nil
          }
          self.fetched[feed] = snapshot.retrievedAt ?? Date()
          self.errors.removeValue(forKey: feed)
        } catch is CancellationError { return } catch {
          guard self.generations[feed] == generation else { return }
          self.errors[feed] = error.localizedDescription
        }
        self.loading.remove(feed)
        if let id = self.selectedID, !self.records.contains(where: { $0.id == id }) {
          self.selectedID = nil
        }
        do { try await Task.sleep(for: .seconds(feed.interval)) } catch { return }
      }
    }
  }
  private func localContext(_ feed: FeedKind) -> String {
    switch feed {
    case .sentinel:
      "\(camera.center.latitude),\(camera.center.longitude):\(sourceConfiguration.sentinelKind)"
    case .atmosphere: "\(camera.center.latitude),\(camera.center.longitude)"
    case .traffic:
      (try? GeoBounds.viewport(camera: camera, aspect: viewportAspect, maximumSpan: 0.05).query)
        ?? "zoom-required"
    case .installations, .energy:
      (try? GeoBounds.viewport(
        camera: camera, aspect: viewportAspect, maximumSpan: feed == .energy ? 2 : 10
      ).query) ?? "zoom-required"
    default: "global"
    }
  }
  private func scheduleLocalRefresh() {
    pendingSelection = nil
    localTask?.cancel()
    let feeds = enabled.intersection([.installations, .energy, .atmosphere, .sentinel, .traffic])
      .filter {
        localContexts[$0] != localContext($0)
      }
    pendingLocalFeeds.formUnion(feeds)
    for feed in feeds {
      localContexts[feed] = localContext(feed)
      generations[feed] = UUID()
      tasks.removeValue(forKey: feed)?.cancel()
      snapshots.removeValue(forKey: feed)
      fetched.removeValue(forKey: feed)
      errors.removeValue(forKey: feed)
      loading.remove(feed)
    }
    if let id = selectedID, !records.contains(where: { $0.id == id && !feeds.contains($0.feed) }) {
      selectedID = nil
    }
    rebuildRecords()
    guard visible, !pendingLocalFeeds.isEmpty else { return }
    localTask = Task { [weak self] in
      do { try await Task.sleep(for: .milliseconds(800)) } catch { return }
      guard let self, !Task.isCancelled, self.visible else { return }
      let pending = self.pendingLocalFeeds
      self.pendingLocalFeeds.removeAll()
      for feed in pending where self.enabled.contains(feed) { self.start(feed) }
    }
  }
  private func updateSolar() {
    solarTask?.cancel()
    solarTask = nil
    guard solarEnabled, visible else {
      solar = nil
      return
    }
    solar = SolarState()
    solarTask = Task { [weak self] in
      while !Task.isCancelled {
        do { try await Task.sleep(for: .seconds(60)) } catch { return }
        guard let self, self.visible, self.solarEnabled else { return }
        self.solar = SolarState()
      }
    }
  }
  func refresh() { for feed in enabled where visible { start(feed) } }
  func pan(dx: Double, dy: Double) {
    trackedID = nil
    routeFlight?.cancel()
    camera.pan(dx: dx, dy: dy)
    enforceSurfaceClearance()
  }
  func zoom(delta: Double) {
    routeFlight?.cancel()
    camera.zoom(delta: delta)
    enforceSurfaceClearance()
  }
  func resetGlobe() {
    trackedID = nil
    routeFlight?.cancel()
    camera = Camera(center: camera.center, altitude: 20_000_000)
  }
  func fly(to coordinate: Coordinate, altitude: Double = 100_000) {
    trackedID = nil
    routeFlight?.cancel()
    camera = Camera(center: coordinate, altitude: altitude)
  }
  func focusSelection() {
    guard let record = selected else { return }
    let local = [FeedKind.installations, .energy].contains(record.feed)
    let altitude =
      record.feed == .satellites
      ? max(1_500_000, (record.altitude ?? 0) + max(1_000_000, (record.altitude ?? 0) * 0.1))
      : local ? min(camera.altitude, 5000) : 100_000
    fly(to: record.coordinate, altitude: altitude)
    if selectedID != record.id { pendingSelection = (record.id, record.feed) }
  }
  var scenePaths: [ScenePath] {
    var result = records.filter { [.cables, .annotations].contains($0.feed) }
      .flatMap { record in
        record.paths.map {
          ScenePath(points: $0, kind: record.feed == .annotations ? .annotation : .reference)
        }
      }
    if let region = sarSearchRegion {
      result.append(ScenePath(points: region.outline, kind: .annotation))
    }
    result += sarSceneFootprints.map { ScenePath(points: $0, kind: .reference) }
    if !reviewOutline.isEmpty {
      result.append(ScenePath(points: reviewOutline, kind: .reference))
    }
    if let route { result.append(ScenePath(points: route.points, kind: .route)) }
    if let estimatedOrbit { result.append(estimatedOrbit) }
    for record in records where record.feed == .traffic {
      let ratio = Double(record.details["Relative flow"] ?? "")
      let kind: ScenePath.Kind =
        record.details["Road closed"] == "Yes"
        ? .trafficJam
        : (ratio ?? 0) >= 0.85 ? .trafficFree : (ratio ?? 0) >= 0.55 ? .trafficSlow : .trafficJam
      result += record.paths.map { ScenePath(points: $0, kind: kind) }
    }
    if drawingPoints.count > 1 {
      result.append(ScenePath(points: drawingPoints, kind: .annotation))
    }
    let fixes = history.fixes(for: trackedID ?? selectedID)
    if showTrails, fixes.count > 1 {
      result.append(
        ScenePath(
          points: fixes.map(\.coordinate),
          altitudes: fixes.allSatisfy { $0.altitude == nil }
            ? []
            : fixes.map {
              max(0, $0.altitude ?? renderer?.surfaceHeight(at: $0.coordinate) ?? 0) + 70
            },
          kind: .trail))
    }
    return result
  }
  func followSelection() {
    guard let record = selected else { return }
    if trackedID == record.id {
      trackedID = nil
      return
    }
    playbackTime = nil
    focusSelection()
    trackedID = record.id
  }
  func requestOrbit() {
    orbitTask?.cancel()
    orbitError = nil
    guard let record = selected, record.feed == .satellites,
      let date = ProviderParsers.date(record.details["Propagation UTC"])
    else { return }
    let group = sourceConfiguration.satelliteGroup
    orbitTask = Task { [weak self, nativeSources] in
      do {
        let path = try await nativeSources.orbit(noradID: record.id, group: group, at: date)
        try Task.checkCancellation()
        guard let self, self.selectedID == record.id else { return }
        self.estimatedOrbit = path
        self.orbitDate = date
      } catch is CancellationError {} catch {
        guard let self, self.selectedID == record.id else { return }
        self.orbitError = error.localizedDescription
      }
    }
  }
  var retainedFixes: [TrackFix] { history.fixes(for: selectedID) }
  var state: WorkspaceState {
    var state = WorkspaceState()
    state.satelliteGroup = sourceConfiguration.satelliteGroup
    state.transitOperator = sourceConfiguration.transitID
    state.cameraProvider = sourceConfiguration.cameraProvider
    state.sentinelCollection = sourceConfiguration.sentinelKind
    state.camera = camera
    state.layers = enabled
    state.basemap = basemap
    state.style = displayStyle
    state.imagery = dailyImagery
    state.opacity = imageryOpacity
    state.solar = solarEnabled
    state.terrain = terrainEnabled
    state.trackedID = trackedID
    state.annotations = annotations
    return state
  }
  func copyLink() {
    do {
      let link = try state.shareURL()
      NSPasteboard.general.clearContents()
      NSPasteboard.general.setString(link.absoluteString, forType: .string)
    } catch { storageError = error.localizedDescription }
  }
  private func restore(_ state: WorkspaceState, includeAnnotations: Bool) {
    trackedID = nil
    playbackTime = nil
    for feed in enabled { setEnabled(feed, false) }
    sourceConfiguration.satelliteGroup = state.satelliteGroup
    sourceConfiguration.transitID = state.transitOperator
    sourceConfiguration.cameraProvider = state.cameraProvider
    sourceConfiguration.sentinelKind = state.sentinelCollection
    camera = state.camera
    basemap = state.basemap
    displayStyle = state.style
    dailyImagery = state.imagery
    imageryOpacity = state.opacity
    solarEnabled = state.solar
    terrainEnabled = state.terrain
    if includeAnnotations { saveAnnotations(state.annotations) }
    for feed in state.layers { setEnabled(feed, true) }
    trackedID = state.trackedID
  }
  func open(_ url: URL) {
    if let state = WorkspaceState.from(url) { restore(state, includeAnnotations: false) }
  }
  func exportWorkspace() {
    let panel = NSSavePanel()
    panel.allowedContentTypes = [.json]
    panel.nameFieldStringValue = "GodEye-workspace.json"
    guard panel.runModal() == .OK, let url = panel.url else { return }
    do {
      let data = try JSONEncoder().encode(state.validated())
      guard data.count <= 2 * 1024 * 1024 else { throw FeedError.oversized }
      try data.write(to: url, options: .atomic)
    } catch {
      storageError = error.localizedDescription
    }
  }
  func importWorkspace() {
    let panel = NSOpenPanel()
    panel.allowedContentTypes = [.json]
    panel.canChooseDirectories = false
    guard panel.runModal() == .OK, let url = panel.url else { return }
    do {
      let state = try JSONDecoder().decode(
        WorkspaceState.self, from: AnnotationStorage.readBounded(url)
      ).validated()
      // Persist first; a filesystem failure must not partially replace the visible workspace.
      try AnnotationStorage.save(state.annotations)
      annotations = state.annotations
      restore(state, includeAnnotations: false)
      updateAnnotations()
      storageError = nil
    } catch { storageError = "Workspace import failed: \(error.localizedDescription)" }
  }
  @discardableResult func saveAnnotations(_ values: [LocalAnnotation]) -> Bool {
    do {
      try AnnotationStorage.save(values)
      annotations = values
      storageError = nil
      updateAnnotations()
      return true
    } catch {
      storageError = error.localizedDescription
      return false
    }
  }
  func addAnnotationAtCenter(_ title: String) {
    let note = LocalAnnotation(title: title, points: [camera.center])
    guard note.isValid else {
      storageError = "Enter a title for the annotation."
      return
    }
    guard saveAnnotations(annotations + [note]) else { return }
    setEnabled(.annotations, true)
  }
  func commitDrawing() {
    let note = LocalAnnotation(title: drawingTitle, points: drawingPoints, closed: closeDrawing)
    guard note.isValid else {
      storageError = "Add a title and enough points for this drawing."
      return
    }
    guard saveAnnotations(annotations + [note]) else { return }
    drawing = false
    drawingPoints = []
    setEnabled(.annotations, true)
  }
  private func updateAnnotations() {
    snapshots[.annotations] = Snapshot(
      records: annotations.compactMap(\.record), rejected: 0, source: "Local annotations")
    rebuildRecords()
  }
  func mapClick(at coordinate: Coordinate) -> Bool {
    if missionPick {
      missionPickedCoordinate = coordinate
      missionPick = false
      revealMission?()
      return true
    }
    if sarRegionSelecting {
      if let first = sarRegionFirstCorner {
        do {
          setSarSearchRegion(try SentinelRegion(corners: first, coordinate))
          let returnToMission = missionAreaCapture
          cancelSarRegionSelection()
          if returnToMission {
            missionAreaCapture = false
            revealMission?()
          } else {
            workspaceSection = "Satellite"
            openPanel(.satellite)
          }
        } catch { sarAcquisition.error = error.localizedDescription }
      } else {
        sarRegionFirstCorner = coordinate
      }
      return true
    }
    if drawing {
      if drawingPoints.count < 2000 { drawingPoints.append(coordinate) }
      return true
    }
    if let endpoint = routeEndpoint {
      if endpoint == "A" { routeA = coordinate } else { routeB = coordinate }
      routeEndpoint = nil
      return true
    }
    return false
  }
  func requestRoute() {
    routeTask?.cancel()
    routeFlight?.cancel()
    routeGeneration = UUID()
    route = nil
    routeError = nil
    guard let a = routeA, let b = routeB else {
      routeError = "Place both endpoints."
      return
    }
    let generation = routeGeneration
    let mode = travelMode
    routeLoading = true
    routeTask = Task { [weak self, transport] in
      do {
        let route = try await transport.route(from: a, to: b, mode: mode)
        try Task.checkCancellation()
        guard let self, self.routeGeneration == generation else { return }
        self.route = route
        self.routeLoading = false
      } catch is CancellationError {} catch {
        guard let self, self.routeGeneration == generation else { return }
        self.routeError = error.localizedDescription
        self.routeLoading = false
      }
    }
  }
  func clearRoute() {
    routeTask?.cancel()
    routeFlight?.cancel()
    routeGeneration = UUID()
    route = nil
    routeA = nil
    routeB = nil
    routeEndpoint = nil
    routeLoading = false
  }
  func flyRoute() {
    routeFlight?.cancel()
    trackedID = nil
    guard let route else { return }
    // Stop at actual route vertices; no synthetic route or terrain-clearance claim.
    let strideSize = max(1, route.points.count / 300)
    let points = stride(from: 0, to: route.points.count, by: strideSize).map { route.points[$0] }
    routeFlight = Task { [weak self] in
      for point in points {
        guard let self, !Task.isCancelled, self.visible else { return }
        self.camera = Camera(
          center: point, altitude: 20_000, heading: self.camera.heading, tilt: self.camera.tilt)
        self.enforceSurfaceClearance()
        do { try await Task.sleep(for: .milliseconds(150)) } catch { return }
      }
    }
  }
  func restartConfiguredSources() {
    for feed in enabled.intersection([.satellites, .fires, .transit, .cameras, .sentinel])
    where visible { start(feed) }
  }
  func search() {
    searchTask?.cancel()
    searchResults = []
    searchError = nil
    let query = searchQuery.trimmingCharacters(in: .whitespacesAndNewlines)
    let generation = UUID()
    searchGeneration = generation
    guard !query.isEmpty else {
      searchLoading = false
      return
    }
    searchLoading = true
    searchTask = Task { [weak self, transport] in
      do {
        let results = try await transport.search(query)
        try Task.checkCancellation()
        guard let self, self.searchGeneration == generation else { return }
        self.searchResults = results
      } catch is CancellationError { return } catch {
        guard let self, self.searchGeneration == generation else { return }
        self.searchError = error.localizedDescription
      }
      guard let self, self.searchGeneration == generation else { return }
      self.searchLoading = false
    }
  }
  func setVisible(_ value: Bool) {
    guard visible != value else { return }
    visible = value
    localTask?.cancel()
    routeFlight?.cancel()
    orbitTask?.cancel()
    if !value {
      cameraRecord = nil
      Task { await ais.stop() }
    }
    pendingLocalFeeds.removeAll()
    updateSolar()
    if value {
      renderer?.resume()
      refresh()
    } else {
      for feed in enabled {
        generations[feed] = UUID()
        tasks.removeValue(forKey: feed)?.cancel()
      }
      loading.removeAll()
      renderer?.suspend()
    }
  }
  func shutdown() {
    missionAutomation.stop()
    missionDetection.cancel()
    sar.cancel()
    satellite.cancel()
    optical.cancelAll()
    sarAcquisition.cancelAll()
    localTask?.cancel()
    solarTask?.cancel()
    routeTask?.cancel()
    routeFlight?.cancel()
    orbitTask?.cancel()
    Task {
      await additionalSources.shutdown()
      await nativeSources.shutdown()
      await traffic.shutdown()
      await ais.shutdown()
    }
    searchTask?.cancel()
    searchGeneration = UUID()
    for feed in enabled { generations[feed] = UUID() }
    for task in tasks.values { task.cancel() }
    tasks.removeAll()
    transport.session.invalidateAndCancel()
    renderer?.stop()
    renderer = nil
  }
}
