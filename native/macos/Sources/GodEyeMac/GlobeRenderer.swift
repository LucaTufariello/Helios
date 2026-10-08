import AppKit
import GodEyeCore
import ImageIO
import MetalKit
import SwiftUI
import simd

private struct Vertex {
  var position: SIMD3<Float>
  var uv: SIMD2<Float>
}
// Metal uses packed_float3: explicit scalar layout keeps CPU/GPU stride identical (20 bytes).
private struct PackedVertex {
  var x: Float
  var y: Float
  var z: Float
  var u: Float
  var v: Float
}
private struct PackedLineVertex {
  var x: Float
  var y: Float
  var z: Float
  var otherX: Float
  var otherY: Float
  var otherZ: Float
  var side: Float
}
private struct Uniforms {
  var viewProjection: simd_float4x4
  var color: SIMD4<Float>
  var settings: SIMD4<Float>
  var eye: SIMD4<Float>
  var offset = SIMD4<Float>(0, 0, 0, 0)
  var texTransform = SIMD4<Float>(1, 1, 0, 0)
}
private struct TileResource {
  var vertex: any MTLBuffer
  var texture: any MTLTexture
  var count: Int
  var origin: SIMD3<Double>
}
private struct SceneResource {
  var vertex: any MTLBuffer
  var indices: any MTLBuffer
  var texture: any MTLTexture
  var count: Int
  var sampler: any MTLSamplerState
  var origin: SIMD3<Double>
  var color: SIMD4<Float>
  var cutoff: Float
  var doubleSided: Bool
  var blend: Bool
}

@MainActor final class GlobeRenderer: NSObject, MTKViewDelegate {
  let device: any MTLDevice
  private let queue: any MTLCommandQueue
  private let textured: any MTLRenderPipelineState
  private let solid: any MTLRenderPipelineState
  private let points: any MTLRenderPipelineState
  private let lines: any MTLRenderPipelineState
  private let meshPipeline: any MTLRenderPipelineState
  private let depth: any MTLDepthStencilState
  private let overlayDepth: any MTLDepthStencilState
  private let loader: MTKTextureLoader
  private let base: any MTLBuffer
  private let baseCount: Int
  private let transport: Transport = {
    let configuration = URLSessionConfiguration.default
    configuration.urlCache = URLCache(
      memoryCapacity: 16 * 1024 * 1024, diskCapacity: 128 * 1024 * 1024)
    configuration.httpMaximumConnectionsPerHost = 4
    configuration.timeoutIntervalForRequest = 20
    return Transport(session: URLSession(configuration: configuration))
  }()
  private var tiles: [RasterKey: TileResource] = [:]
  private var desired = Set<RasterKey>()
  private var tasks: [RasterKey: Task<Void, Never>] = [:]
  private var requestTokens: [RasterKey: UUID] = [:]
  private final class RasterLayer {
    var overlay: SarOverlay
    var resources: [TileID: TileResource] = [:]
    var texture: (any MTLTexture)?
    var task: Task<Void, Never>?
    init(_ overlay: SarOverlay) { self.overlay = overlay }
  }
  private var rasterLayers: [RasterLayer] = []
  var onSarStatus: ((String?) -> Void)?
  private var suspended = false
  private var retryAfter: [RasterKey: Date] = [:]
  private var scenePaths: [ScenePath] = []
  private var pathBuffers: [(any MTLBuffer, Int, SIMD4<Float>)] = []
  private var basemap = Basemap.satellite
  private var style = DisplayStyle.standard
  private var solarBuffer: (any MTLBuffer)?
  private var solarCount = 0
  private var solar: SolarState?
  private var footprintBuffer: (any MTLBuffer)?
  private var footprintCount = 0
  var onViewport: ((Double) -> Void)?
  private var markerBuffer: (any MTLBuffer)?
  private var recordGroups: [(Int, Int, SIMD4<Float>)] = []
  private(set) var records: [GeoRecord] = []
  private(set) var renderedFrames = 0
  private(set) var tileFailures = 0
  private(set) var loadedTiles = 0
  var dailyImagery = DailyImagery.off
  var imageryDate = DailyImagery.date()
  var imageryOpacity = 0.85
  var camera = Camera.initial
  var selected: String?
  weak var view: MTKView?
  var onStatus: ((String) -> Void)?
  var onTerrainStatus: ((String) -> Void)?
  var onSceneStatus: ((String, String) -> Void)?
  var onSurfaceUpdated: (() -> Void)?
  private var terrainEnabled = true
  private var terrain = TerrainSurface()
  private var terrainTasks: [TileID: Task<Void, Never>] = [:]
  private var terrainTokens: [TileID: UUID] = [:]
  private var terrainWanted: Set<TileID> = []
  private var terrainFailed: Set<TileID> = []
  private var geometryTask: Task<Void, Never>?
  private var nativeScene: NativeTileset?
  private var tilesetURL: URL?
  private var sceneTask: Task<Void, Never>?
  private var sceneGeneration = UUID()
  private var sceneResources: [UInt64: SceneResource] = [:]
  private var sceneTextures: [UInt64: any MTLTexture] = [:]
  private var visibleMeshes: [UInt64] = []
  private var sceneStatus = ""
  private(set) var pickEvidence = ""
  private var cachedCenter: Coordinate?
  private var cachedHeight: Double = 0
  private var pose: CameraPose {
    if cachedCenter != camera.center {
      cachedCenter = camera.center
      cachedHeight = surfaceHeight(at: camera.center) ?? 0
    }
    return CameraPose(camera: camera, targetHeight: cachedHeight)
  }

  init(view: MTKView) throws {
    guard let device = view.device, let queue = device.makeCommandQueue() else {
      throw NativeError.gpuUnavailable
    }
    self.device = device
    self.queue = queue
    self.loader = MTKTextureLoader(device: device)
    guard
      let sourceURL = Bundle.module.url(
        forResource: "Globe", withExtension: "metal", subdirectory: "Resources")
    else { throw NativeError.resourcesMissing }
    let library: any MTLLibrary
    if let compiled = Bundle.module.url(
      forResource: "Globe", withExtension: "metallib", subdirectory: "Resources")
    {
      library = try device.makeLibrary(URL: compiled)
    } else {
      library = try device.makeLibrary(
        source: String(contentsOf: sourceURL, encoding: .utf8), options: nil)
    }
    func pipeline(_ fragment: String, vertex: String = "tileVertex") throws
      -> any MTLRenderPipelineState
    {
      let description = MTLRenderPipelineDescriptor()
      description.vertexFunction = library.makeFunction(name: vertex)
      description.fragmentFunction = library.makeFunction(name: fragment)
      description.colorAttachments[0].pixelFormat = .bgra8Unorm
      description.depthAttachmentPixelFormat = .depth32Float
      description.colorAttachments[0].isBlendingEnabled = true
      description.colorAttachments[0].sourceRGBBlendFactor = .sourceAlpha
      description.colorAttachments[0].destinationRGBBlendFactor = .oneMinusSourceAlpha
      description.colorAttachments[0].sourceAlphaBlendFactor = .one
      description.colorAttachments[0].destinationAlphaBlendFactor = .oneMinusSourceAlpha
      return try device.makeRenderPipelineState(descriptor: description)
    }
    textured = try pipeline("tileFragment")
    solid = try pipeline("solidFragment")
    points = try pipeline("markerFragment")
    lines = try pipeline("solidFragment", vertex: "lineVertex")
    meshPipeline = try pipeline("meshFragment")
    let depthDescription = MTLDepthStencilDescriptor()
    depthDescription.depthCompareFunction = .greaterEqual
    depthDescription.isDepthWriteEnabled = true
    guard let depth = device.makeDepthStencilState(descriptor: depthDescription) else {
      throw NativeError.gpuUnavailable
    }
    self.depth = depth
    let overlayDescription = MTLDepthStencilDescriptor()
    overlayDescription.depthCompareFunction = .greaterEqual
    overlayDescription.isDepthWriteEnabled = false
    guard let overlayDepth = device.makeDepthStencilState(descriptor: overlayDescription) else {
      throw NativeError.gpuUnavailable
    }
    self.overlayDepth = overlayDepth
    let vertices = Self.sphereVertices()
    guard
      let base = device.makeBuffer(
        bytes: vertices, length: vertices.count * MemoryLayout<PackedVertex>.stride,
        options: .storageModeShared)
    else { throw NativeError.gpuUnavailable }
    self.base = base
    baseCount = vertices.count
    super.init()
    self.view = view
    view.clearDepth = 0
    view.depthStencilPixelFormat = .depth32Float
    view.colorPixelFormat = .bgra8Unorm
    view.clearColor = MTLClearColor(red: 0.025, green: 0.035, blue: 0.055, alpha: 1)
    view.isPaused = true
    view.enableSetNeedsDisplay = true
    view.delegate = self
  }
  func update(
    camera: Camera, records: [GeoRecord], selected: String?, dailyImagery: DailyImagery = .off,
    opacity: Double = 0.85, solar: SolarState? = nil, basemap: Basemap = .satellite,
    style: DisplayStyle = .standard, paths: [ScenePath] = [], terrainEnabled: Bool = true,
    tilesetURL: URL? = nil
  ) {
    let changedTerrain = self.terrainEnabled != terrainEnabled
    if changedTerrain { cachedCenter = nil }
    self.terrainEnabled = terrainEnabled
    if self.tilesetURL != tilesetURL {
      self.tilesetURL = tilesetURL
      stopScene()
      if let tilesetURL, !suspended {
        do { nativeScene = try NativeTileset(url: tilesetURL) } catch {
          onSceneStatus?(error.localizedDescription, "")
        }
      }
    }
    if changedTerrain { rebuildTerrainGeometry() }
    let changedBasemap = self.basemap != basemap
    let changedStyle = self.style != style
    self.basemap = basemap
    self.style = style
    let changedPaths = scenePaths != paths
    if changedPaths {
      scenePaths = paths
      pathBuffers = []
      for kind in [
        ScenePath.Kind.reference, .route, .annotation, .trail, .orbit, .trafficFree, .trafficSlow,
        .trafficJam,
      ] {
        let vertices = paths.filter { $0.kind == kind }.flatMap {
          lineVertices($0.points, altitude: 70, altitudes: $0.altitudes)
        }
        let color: SIMD4<Float>
        switch kind {
        case .route: color = SIMD4(0.3, 0.85, 1, 1)
        case .annotation: color = SIMD4(1, 0.55, 0.3, 1)
        case .trail: color = SIMD4(1, 0.9, 0.45, 1)
        case .orbit: color = SIMD4(0.8, 0.7, 1, 1)
        case .trafficFree: color = SIMD4(46 / 255, 204 / 255, 113 / 255, 0.9)
        case .trafficSlow: color = SIMD4(240 / 255, 178 / 255, 62 / 255, 0.9)
        case .trafficJam: color = SIMD4(224 / 255, 82 / 255, 82 / 255, 0.9)
        case .reference: color = SIMD4(0.35, 0.8, 0.7, 0.8)
        }
        if !vertices.isEmpty,
          let buffer = device.makeBuffer(
            bytes: vertices, length: vertices.count * MemoryLayout<PackedLineVertex>.stride,
            options: .storageModeShared)
        {
          pathBuffers.append((buffer, vertices.count, color))
        }
      }
    }
    let date = DailyImagery.date()
    let changedImagery = self.dailyImagery != dailyImagery || self.imageryDate != date
    let changedOpacity = self.imageryOpacity != opacity
    self.dailyImagery = dailyImagery
    self.imageryDate = date
    self.imageryOpacity = opacity
    let changedCamera = self.camera != camera
    let changedRecords = self.records != records
    let changedSelection = self.selected != selected
    let changedSolar = self.solar != solar
    if changedSolar {
      self.solar = solar
      let vertices = lineVertices(solar?.terminator ?? [], altitude: 50)
      solarCount = vertices.count
      solarBuffer =
        vertices.isEmpty
        ? nil
        : device.makeBuffer(
          bytes: vertices, length: vertices.count * MemoryLayout<PackedLineVertex>.stride,
          options: .storageModeShared)
    }
    if changedSelection || changedRecords {
      let outline = records.first(where: { $0.id == selected })?.footprint ?? []
      let vertices = lineVertices(outline, altitude: 50)
      footprintCount = vertices.count
      footprintBuffer =
        vertices.isEmpty
        ? nil
        : device.makeBuffer(
          bytes: vertices, length: vertices.count * MemoryLayout<PackedLineVertex>.stride,
          options: .storageModeShared)
    }
    self.camera = camera
    self.selected = selected
    if changedRecords || changedSelection {
      self.records = records
      var vertices: [PackedVertex] = []
      recordGroups = []
      for feed in FeedKind.allCases {
        let start = vertices.count
        for record in records where record.feed == feed && record.id != selected {
          let position =
            record.coordinate.ecef(
              altitude: max(
                0,
                record.altitude
                  ?? (terrainEnabled ? renderedGroundHeight(at: record.coordinate) ?? 0 : 0)) + 50)
            / 6_378_137
          vertices.append(
            .init(x: Float(position.x), y: Float(position.y), z: Float(position.z), u: 0, v: 0))
        }
        if vertices.count > start {
          recordGroups.append((start, vertices.count - start, Self.markerColor(feed: feed)))
        }
      }
      markerBuffer =
        vertices.isEmpty
        ? nil
        : device.makeBuffer(
          bytes: vertices, length: vertices.count * MemoryLayout<PackedVertex>.stride,
          options: .storageModeShared)
    }
    if changedCamera || changedImagery || changedBasemap || desired.isEmpty { requestTiles() }
    if changedCamera || changedTerrain { requestTerrain() }
    if nativeScene != nil { startScenePolling() }
    if changedCamera || changedRecords || changedSelection || changedImagery || changedOpacity
      || changedSolar || changedPaths || changedStyle || changedBasemap || changedTerrain
    {
      view?.needsDisplay = true
    }
  }
  private func requestTiles() {
    let baseTiles = (0..<2).flatMap { y in (0..<2).compactMap { x in TileID(x: x, y: y, zoom: 1) } }
    desired = Set(
      (baseTiles
        + TileID.visible(
          camera: camera, aspect: max(1, view?.bounds.width ?? 1) / max(1, view?.bounds.height ?? 1)
        )).map { RasterKey(tile: $0, basemap: basemap) })
    if dailyImagery != .off {
      desired.formUnion(
        TileID.visible(
          camera: camera, maximumZoom: 9,
          aspect: max(1, view?.bounds.width ?? 1) / max(1, view?.bounds.height ?? 1)
        ).map {
          RasterKey(tile: $0, imagery: dailyImagery, date: imageryDate)
        })
    }
    for id in Array(tasks.keys) where !desired.contains(id) {
      tasks.removeValue(forKey: id)?.cancel()
      requestTokens.removeValue(forKey: id)
    }
    tiles = tiles.filter { desired.contains($0.key) }
    for layer in rasterLayers {
      layer.resources = layer.resources.filter {
        tiles[RasterKey(tile: $0.key, basemap: basemap)] != nil
      }
    }
    retryAfter = retryAfter.filter { $0.value > Date() }
    pump()
    // Update labels even when cached tiles need no additional network request.
    Task { @MainActor [weak self] in self?.publishTileStatus() }
  }
  private func publishTileStatus() {
    let baseReady = self.tiles.keys.contains { $0.imagery == .off }
    let overlayRequested = self.desired.filter { $0.imagery != .off }
    let overlayReady = overlayRequested.filter { self.tiles[$0] != nil }.count
    let overlayFailed = overlayRequested.contains { self.retryAfter[$0] != nil }
    var status = baseReady ? self.basemap.name : "Basemap unavailable"
    if self.dailyImagery != .off {
      status +=
        " · NASA daily imagery "
        + (overlayFailed
          ? "partially unavailable"
          : overlayReady == overlayRequested.count ? "ready" : "loading")
        + " · \(self.imageryDate) UTC"
    }
    self.onStatus?(status)
  }
  private func pump() {
    guard !suspended else { return }
    // Four concurrent requests, viewport-bounded GPU storage; ordinary bounded HTTP caching.
    for id in desired.sorted(by: { a, b in
      if a.tile.zoom != b.tile.zoom { return a.tile.zoom < b.tile.zoom }
      if a.imagery != b.imagery { return a.imagery.rawValue < b.imagery.rawValue }
      let center = TileID.containing(camera.center, zoom: a.tile.zoom)
      let count = 1 << a.tile.zoom
      func distance(_ tile: TileID) -> Int {
        let dx = abs(tile.x - center.x)
        return min(dx, count - dx) + abs(tile.y - center.y)
      }
      return distance(a.tile) < distance(b.tile)
    }) {
      guard tasks.count < 4 else { break }
      guard tiles[id] == nil, tasks[id] == nil, retryAfter[id] == nil else { continue }
      let token = UUID()
      requestTokens[id] = token
      tasks[id] = Task { [weak self, transport] in
        do {
          guard let url = id.url else { throw FeedError.malformed }
          let data = try await transport.data(url, maximumBytes: 2 * 1_024 * 1_024)
          try Task.checkCancellation()
          guard let self, self.desired.contains(id), self.requestTokens[id] == token,
            !self.suspended
          else { return }
          let decoded = try RasterImage.decode(data)
          let image = try await self.loader.newTexture(
            cgImage: decoded,
            options: [
              .SRGB: false, .generateMipmaps: false, .origin: MTKTextureLoader.Origin.topLeft,
            ])
          try Task.checkCancellation()
          guard self.requestTokens[id] == token, self.desired.contains(id), !self.suspended else {
            return
          }
          let vertices = vertices(tile: id.tile)
          if let buffer = self.device.makeBuffer(
            bytes: vertices, length: vertices.count * MemoryLayout<PackedVertex>.stride,
            options: .storageModeShared)
          {
            self.tiles[id] = TileResource(
              vertex: buffer, texture: image, count: vertices.count,
              origin: id.tile.coordinate(u: 0.5, v: 0.5).ecef())
            self.rebuildSarGeometry()
            self.loadedTiles += 1
            self.geometryTask?.cancel()
            self.geometryTask = Task { [weak self] in
              do { try await Task.sleep(for: .milliseconds(80)) } catch { return }
              self?.rebuildTerrainGeometry()
              self?.onSurfaceUpdated?()
            }
            self.view?.needsDisplay = true
          }
        } catch is CancellationError {} catch {
          guard let self, self.requestTokens[id] == token else { return }
          self.tileFailures += 1
          self.retryAfter[id] = Date().addingTimeInterval(30)
        }
        guard let self, self.requestTokens[id] == token else { return }
        self.requestTokens.removeValue(forKey: id)
        self.tasks.removeValue(forKey: id)
        self.publishTileStatus()
        self.pump()
      }
    }
  }
  func suspend() {
    suspended = true
    stopScene()
    cancelTerrain()
    for task in tasks.values { task.cancel() }
    tasks.removeAll()
    requestTokens.removeAll()
  }
  func resume() {
    suspended = false
    if let tilesetURL {
      nativeScene = try? NativeTileset(url: tilesetURL)
      startScenePolling()
    }
    requestTerrain()
    requestTiles()
    view?.needsDisplay = true
  }
  func stop() {
    for layer in rasterLayers { layer.task?.cancel() }
    rasterLayers.removeAll()
    stopScene()
    cancelTerrain()
    for task in tasks.values { task.cancel() }
    tasks.removeAll()
    desired.removeAll()
    requestTokens.removeAll()
    transport.session.invalidateAndCancel()
    view?.delegate = nil
  }
  func mtkView(_ view: MTKView, drawableSizeWillChange size: CGSize) {
    view.needsDisplay = true
    if size.height > 0 { onViewport?(size.width / size.height) }
    requestTiles()
    requestTerrain()
    startScenePolling()
  }
  private func matrix(size: CGSize) -> simd_float4x4 {
    pose.matrix(aspect: max(1, size.width) / max(1, size.height))
  }
  private func meshHeight(at coordinate: Coordinate, tile id: TileID, resource: TileResource)
    -> Double?
  {
    let n = Double(1 << id.zoom)
    var x = (coordinate.longitude + 180) / 360 * n
    if x - Double(id.x) > n / 2 { x -= n }
    if x - Double(id.x) < -n / 2 { x += n }
    let latitude = min(85.05112878, max(-85.05112878, coordinate.latitude)) * .pi / 180
    let y = (1 - log(tan(latitude) + 1 / cos(latitude)) / .pi) / 2 * n
    let u = x - Double(id.x)
    let v = y - Double(id.y)
    guard u >= -1e-8, u <= 1 + 1e-8, v >= -1e-8, v <= 1 + 1e-8 else { return nil }
    let divisions = Int(sqrt(Double(resource.count / 6)))
    guard divisions > 0 else { return nil }
    let ix = min(divisions - 1, max(0, Int(u * Double(divisions))))
    let iy = min(divisions - 1, max(0, Int(v * Double(divisions))))
    let lat = coordinate.radians.y
    let lon = coordinate.radians.x
    guard
      let ray = SceneRay(
        origin: coordinate.ecef(altitude: 20_000),
        direction: -SIMD3(cos(lat) * cos(lon), cos(lat) * sin(lon), sin(lat)))
    else { return nil }
    let vertices = resource.vertex.contents().assumingMemoryBound(to: PackedVertex.self)
    func world(_ i: Int) -> SIMD3<Double> {
      let p = vertices[i]
      return resource.origin + SIMD3(Double(p.x), Double(p.y), Double(p.z)) * 6_378_137
    }
    var nearest = Double.infinity
    // Spherical projection may cross a neighboring grid cell; check a bounded 3×3 neighborhood.
    for cy in max(0, iy - 1)...min(divisions - 1, iy + 1) {
      for cx in max(0, ix - 1)...min(divisions - 1, ix + 1) {
        let start = (cy * divisions + cx) * 6
        for i in [start, start + 3] {
          if let d = ray.triangle(world(i), world(i + 1), world(i + 2)) {
            nearest = min(nearest, d)
          }
        }
      }
    }
    guard nearest.isFinite else { return nil }
    return SurfacePoint(ecef: ray.origin + ray.direction * nearest)?.height
  }
  private func renderedGroundHeight(at coordinate: Coordinate) -> Double? {
    let candidates = tiles.filter {
      $0.key.imagery == .off && TileID.containing(coordinate, zoom: $0.key.tile.zoom) == $0.key.tile
    }.sorted { $0.key.tile.zoom > $1.key.tile.zoom }
    for (key, tile) in candidates {
      if let height = meshHeight(at: coordinate, tile: key.tile, resource: tile) { return height }
    }
    return terrainEnabled ? terrain.groundHeight(at: coordinate) : 0
  }
  func surfaceHeight(at coordinate: Coordinate) -> Double? {
    let ground = renderedGroundHeight(at: coordinate)
    let lat = coordinate.radians.y
    let lon = coordinate.radians.x
    let normal = SIMD3<Double>(cos(lat) * cos(lon), cos(lat) * sin(lon), sin(lat))
    if let ray = SceneRay(origin: coordinate.ecef(altitude: 20_000), direction: -normal),
      let hit = nativeScene?.pick(ray), let point = SurfacePoint(ecef: hit)
    {
      return max(ground ?? -.infinity, point.height)
    }
    return ground
  }
  private func coverageMasks(parent: TileID, loaded: [TileID]) -> [SIMD4<Float>] {
    loaded.compactMap { child in
      guard child.zoom > parent.zoom else { return nil }
      let scale = 1 << (child.zoom - parent.zoom)
      guard child.x / scale == parent.x, child.y / scale == parent.y else { return nil }
      let size = 1 / Float(scale)
      let u = Float(child.x % scale) * size
      let v = Float(child.y % scale) * size
      return SIMD4(u, v, u + size, v + size)
    }
  }
  private func surfacePick(_ ray: SceneRay) -> SIMD3<Double>? {
    var nearest: SIMD3<Double>?
    var distance = Double.infinity
    // Test the actual raster/terrain triangles submitted to Metal, including their level of detail.
    for (key, tile) in tiles where key.imagery == .off {
      let radius =
        [SIMD2(0.0, 0.0), SIMD2(1.0, 0.0), SIMD2(0.0, 1.0), SIMD2(1.0, 1.0)].map {
          simd_distance(tile.origin, key.tile.coordinate(u: $0.x, v: $0.y).ecef())
        }.max()! + 12_000
      let projection = simd_dot(tile.origin - ray.origin, ray.direction)
      guard projection + radius >= 0,
        simd_length(ray.origin + max(0, projection) * ray.direction - tile.origin) <= radius
      else { continue }
      let vertices = tile.vertex.contents().assumingMemoryBound(to: PackedVertex.self)
      func world(_ index: Int) -> SIMD3<Double> {
        let p = vertices[index]
        return tile.origin + SIMD3(Double(p.x), Double(p.y), Double(p.z)) * 6_378_137
      }
      for i in stride(from: 0, to: tile.count, by: 3) {
        if let d = ray.triangle(world(i), world(i + 1), world(i + 2)), d < distance {
          let hit = ray.origin + ray.direction * d
          if let point = SurfacePoint(ecef: hit),
            tiles.keys.contains(where: {
              $0.imagery == .off && $0.tile.zoom > key.tile.zoom
                && TileID.containing(point.coordinate, zoom: $0.tile.zoom) == $0.tile
            })
          {
            continue
          }
          distance = d
          nearest = hit
        }
      }
    }
    if nearest == nil, let d = ray.ellipsoidDistance() {
      nearest = ray.origin + ray.direction * d
      distance = d
    }
    if let mesh = nativeScene?.pick(ray), simd_distance(mesh, ray.origin) < distance {
      nearest = mesh
    }
    return nearest
  }
  func pick(location: CGPoint, size: CGSize) -> SurfacePoint? {
    guard
      let ray = pose.ray(
        x: 2 * location.x / max(1, size.width) - 1, y: 2 * location.y / max(1, size.height) - 1,
        aspect: max(1, size.width) / max(1, size.height)),
      let hit = surfacePick(ray), let point = SurfacePoint(ecef: hit)
    else { return nil }
    if let mesh = nativeScene?.pick(ray), simd_distance(mesh, hit) < 2 {
      pickEvidence = "Loaded 3D tile geometry; model acquisition time unknown"
    } else if terrainEnabled && terrain.height(at: point.coordinate) != nil {
      pickEvidence =
        "Mapzen elevation model; acquisition time and local vertical datum not provided"
    } else {
      pickEvidence = "WGS84 ellipsoid fallback; terrain elevation unavailable"
    }
    return point
  }
  private func cancelTerrain() {
    for task in terrainTasks.values { task.cancel() }
    terrainTasks.removeAll()
    terrainTokens.removeAll()
    geometryTask?.cancel()
    geometryTask = nil
  }
  private func requestTerrain() {
    guard !suspended else { return }
    let wanted: Set<TileID> =
      terrainEnabled && camera.altitude <= 200_000
      ? Set(
        TileID.visible(
          camera: camera, maximumZoom: 14,
          aspect: max(1, view?.bounds.width ?? 1) / max(1, view?.bounds.height ?? 1))) : []
    if wanted != terrainWanted { terrainFailed.removeAll() }
    terrainWanted = wanted
    terrain.retain(wanted)
    for id in Array(terrainTasks.keys) where !wanted.contains(id) {
      terrainTasks.removeValue(forKey: id)?.cancel()
      terrainTokens.removeValue(forKey: id)
    }
    pumpTerrain()
  }
  private func pumpTerrain() {
    guard !suspended else { return }
    let center = camera.center
    let ordered = terrainWanted.sorted {
      simd_distance($0.coordinate(u: 0.5, v: 0.5).ecef(), center.ecef())
        < simd_distance($1.coordinate(u: 0.5, v: 0.5).ecef(), center.ecef())
    }
    for id in ordered {
      guard terrainTasks.count < 4 else { break }
      guard terrain.tiles[id] == nil, terrainTasks[id] == nil, !terrainFailed.contains(id) else {
        continue
      }
      let token = UUID()
      terrainTokens[id] = token
      terrainTasks[id] = Task { [weak self, transport] in
        do {
          let bytes = try await transport.data(ElevationTile.url(id), maximumBytes: 1024 * 1024)
          let tile = try await Task.detached(priority: .userInitiated) {
            try ElevationTile.decode(bytes, id: id)
          }.value
          try Task.checkCancellation()
          guard let self, self.terrainTokens[id] == token, !self.suspended else { return }
          self.terrain.insert(tile)
          self.cachedCenter = nil
          self.geometryTask?.cancel()
          self.geometryTask = Task { [weak self] in
            do { try await Task.sleep(for: .milliseconds(80)) } catch { return }
            self?.rebuildTerrainGeometry()
            self?.onSurfaceUpdated?()
          }
        } catch is CancellationError {} catch {
          guard let self, self.terrainTokens[id] == token else { return }
          self.terrainFailed.insert(id)
        }
        guard let self, self.terrainTokens[id] == token else { return }
        self.terrainTokens.removeValue(forKey: id)
        self.terrainTasks.removeValue(forKey: id)
        self.pumpTerrain()
      }
    }
    let loaded = terrainWanted.filter { terrain.tiles[$0] != nil }.count
    onTerrainStatus?(
      terrainEnabled
        ? terrainWanted.isEmpty
          ? "Terrain: zoom below 200 km to load elevation"
          : "Terrain: \(loaded)/\(terrainWanted.count) tiles\(terrainFailed.isEmpty ? "" : " · some unavailable")"
        : "Terrain off · smooth ellipsoid")
  }
  private func rebuildTerrainGeometry() {
    for (key, resource) in tiles.sorted(by: { $0.key.tile.zoom < $1.key.tile.zoom }) {
      let vertices = vertices(tile: key.tile)
      if let buffer = device.makeBuffer(
        bytes: vertices, length: vertices.count * MemoryLayout<PackedVertex>.stride,
        options: .storageModeShared)
      {
        tiles[key] = .init(
          vertex: buffer, texture: resource.texture, count: vertices.count, origin: resource.origin)
      }
    }
    rebuildSarGeometry()
    // Reuse normal update logic to drape the existing paths and selection outlines.
    let paths = scenePaths
    scenePaths = []
    let records = self.records
    self.records = []
    update(
      camera: camera, records: records, selected: selected, dailyImagery: dailyImagery,
      opacity: imageryOpacity,
      solar: solar, basemap: basemap, style: style, paths: paths, terrainEnabled: terrainEnabled,
      tilesetURL: tilesetURL)
    view?.needsDisplay = true
  }
  func setRasterOverlays(_ overlays: [SarOverlay]) {
    // At most two bounded preview layers. Changing opacity reuses GPU resources.
    let overlays = Array(overlays.filter { ChangeReviewResult.validBounds($0.bounds) }.prefix(2))
    guard overlays != rasterLayers.map(\.overlay) else { return }
    let old = rasterLayers
    var next: [RasterLayer] = []
    for overlay in overlays {
      if let existing = old.first(where: {
        $0.overlay.preview == overlay.preview && $0.overlay.bounds == overlay.bounds
      }) {
        existing.overlay = overlay
        next.append(existing)
      } else {
        next.append(RasterLayer(overlay))
      }
    }
    for layer in old where !next.contains(where: { $0 === layer }) { layer.task?.cancel() }
    rasterLayers = next
    publishRasterStatus()
    view?.needsDisplay = true
    for layer in rasterLayers where layer.texture == nil && layer.task == nil {
      let overlay = layer.overlay
      layer.task = Task { [weak self, weak layer] in
        do {
          let image = try await Task.detached {
            guard let size = try overlay.preview.resourceValues(forKeys: [.fileSizeKey]).fileSize,
              size <= 32 * 1024 * 1024,
              let source = CGImageSourceCreateWithURL(overlay.preview as CFURL, nil),
              let properties = CGImageSourceCopyPropertiesAtIndex(source, 0, nil) as? [String: Any],
              let width = properties[kCGImagePropertyPixelWidth as String] as? Int,
              let height = properties[kCGImagePropertyPixelHeight as String] as? Int,
              width > 0, height > 0, width <= 2048, height <= 2048,
              let image = CGImageSourceCreateImageAtIndex(source, 0, nil)
            else { throw SarError.failed("The saved raster preview cannot be decoded safely.") }
            return image
          }.value
          guard let self, let layer else { return }
          let texture = try await self.loader.newTexture(
            cgImage: image,
            options: [.SRGB: false, .origin: MTKTextureLoader.Origin.topLeft])
          try Task.checkCancellation()
          guard self.rasterLayers.contains(where: { $0 === layer }) else { return }
          layer.texture = texture
          self.rebuildSarGeometry()
          self.publishRasterStatus()
          self.view?.needsDisplay = true
        } catch {
          if !Task.isCancelled {
            self?.onSarStatus?("Saved raster overlay unavailable: " + error.localizedDescription)
          }
        }
      }
    }
  }
  private func publishRasterStatus() {
    guard !rasterLayers.isEmpty else {
      onSarStatus?(nil)
      return
    }
    let ready = rasterLayers.allSatisfy { $0.texture != nil }
    onSarStatus?(
      (rasterLayers.count == 2
        ? "Visual comparison · 2 georeferenced preview layers" : rasterLayers[0].overlay.label)
        + (ready ? " · bounded display preview" : " · loading preview"))
  }
  private func sarUV(_ coordinate: Coordinate, bounds: [Double]) -> SIMD2<Float> {
    SIMD2(
      Float((coordinate.longitude - bounds[0]) / (bounds[2] - bounds[0])),
      Float((bounds[3] - coordinate.latitude) / (bounds[3] - bounds[1])))
  }
  private func rebuildSarGeometry() {
    for layer in rasterLayers {
      layer.resources.removeAll()
      guard let texture = layer.texture else { continue }
      let b = layer.overlay.bounds
      for (key, tile) in tiles where key.imagery == .off {
        let northwest = key.tile.coordinate(u: 0, v: 0)
        let southeast = key.tile.coordinate(u: 1, v: 1)
        guard northwest.longitude < b[2], southeast.longitude > b[0],
          northwest.latitude > b[1], southeast.latitude < b[3]
        else { continue }
        let pointer = tile.vertex.contents().bindMemory(to: PackedVertex.self, capacity: tile.count)
        var vertices = Array(UnsafeBufferPointer(start: pointer, count: tile.count))
        for i in vertices.indices {
          let coordinate = key.tile.coordinate(u: Double(vertices[i].u), v: Double(vertices[i].v))
          let uv = sarUV(coordinate, bounds: b)
          vertices[i].u = uv.x
          vertices[i].v = uv.y
        }
        // Positions/origins match the visible raster mesh exactly, so reverse-Z
        // equality preserves the layer without guessed altitude offsets or mesh gaps.
        if let buffer = device.makeBuffer(
          bytes: vertices,
          length: vertices.count * MemoryLayout<PackedVertex>.stride, options: .storageModeShared)
        {
          layer.resources[key.tile] = TileResource(
            vertex: buffer, texture: texture,
            count: vertices.count, origin: tile.origin)
        }
      }
    }
  }
  private func stopScene() {
    sceneGeneration = UUID()
    sceneTask?.cancel()
    sceneTask = nil
    nativeScene?.close()
    nativeScene = nil
    cachedCenter = nil
    visibleMeshes = []
    sceneResources = [:]
    sceneTextures = [:]
  }
  private func startScenePolling() {
    guard nativeScene != nil, sceneTask == nil, !suspended else { return }
    let generation = UUID()
    sceneGeneration = generation
    sceneTask = Task { [weak self] in
      var stable = 0
      let deadline = Date().addingTimeInterval(90)
      while !Task.isCancelled {
        guard let self else { return }
        let changed = self.updateScene()
        if ((self.nativeScene?.progress ?? 0) >= 100
          || ((self.nativeScene?.failures ?? 0) > 0 && self.visibleMeshes.isEmpty)) && !changed
        {
          stable += 1
        } else {
          stable = 0
        }
        if stable >= 3 || Date() > deadline { break }
        do { try await Task.sleep(for: .milliseconds(100)) } catch { return }
      }
      if self?.sceneGeneration == generation { self?.sceneTask = nil }
    }
  }
  @discardableResult private func updateScene() -> Bool {
    guard let nativeScene, let view, !suspended else { return false }
    let count = nativeScene.update(
      pose: pose, width: max(1, view.drawableSize.width), height: max(1, view.drawableSize.height))
    let candidates = (0..<min(count, 20_000)).compactMap { nativeScene.mesh(at: $0) }
    let needed = Set(candidates.map(\.id))
    let neededTextures = Set(candidates.map(\.textureID))
    sceneResources = sceneResources.filter { needed.contains($0.key) }
    sceneTextures = sceneTextures.filter { neededTextures.contains($0.key) }
    var visible: [UInt64] = []
    var textureIDs: Set<UInt64> = []
    var bytes =
      sceneResources.values.reduce(0) { $0 + $1.vertex.allocatedSize + $1.indices.allocatedSize }
      + sceneTextures.values.reduce(0) { $0 + $1.allocatedSize }
    var exceeded = false
    for mesh in candidates {
      textureIDs.insert(mesh.textureID)
      if sceneResources[mesh.id] == nil {
        let vertexBytes = Int(mesh.vertexCount) * MemoryLayout<PackedVertex>.stride
        let indexBytes = Int(mesh.indexCount) * 4
        let textureBytes =
          sceneTextures[mesh.textureID] == nil
          ? max(4, Int(mesh.width) * Int(mesh.height) * 4 * 4 / 3) : 0
        guard vertexBytes > 0, indexBytes > 0,
          bytes + vertexBytes + indexBytes + textureBytes <= 256 * 1024 * 1024,
          let vertices = mesh.vertices, let indices = mesh.indices,
          let vertex = device.makeBuffer(
            bytes: vertices, length: vertexBytes, options: .storageModeShared),
          let index = device.makeBuffer(
            bytes: indices, length: indexBytes, options: .storageModeShared)
        else {
          exceeded = true
          continue
        }
        let texture: any MTLTexture
        if let cached = sceneTextures[mesh.textureID] {
          texture = cached
        } else {
          let description = MTLTextureDescriptor.texture2DDescriptor(
            pixelFormat: .rgba8Unorm_srgb, width: max(1, Int(mesh.width)),
            height: max(1, Int(mesh.height)), mipmapped: true)
          description.storageMode = .shared
          guard let image = device.makeTexture(descriptor: description) else {
            exceeded = true
            continue
          }
          if let pixels = mesh.pixels {
            image.replace(
              region: MTLRegionMake2D(0, 0, Int(mesh.width), Int(mesh.height)), mipmapLevel: 0,
              withBytes: pixels, bytesPerRow: Int(mesh.width) * 4)
          } else {
            var white: UInt32 = .max
            image.replace(
              region: MTLRegionMake2D(0, 0, 1, 1), mipmapLevel: 0, withBytes: &white, bytesPerRow: 4
            )
          }
          if let command = queue.makeCommandBuffer(), let blit = command.makeBlitCommandEncoder() {
            blit.generateMipmaps(for: image)
            blit.endEncoding()
            command.commit()
          }
          sceneTextures[mesh.textureID] = image
          texture = image
          bytes += texture.allocatedSize
        }
        let sampling = MTLSamplerDescriptor()
        func address(_ code: Int32) -> MTLSamplerAddressMode {
          switch code {
          case 33071: return .clampToEdge
          case 33648: return .mirrorRepeat
          default: return .repeat
          }
        }
        sampling.sAddressMode = address(mesh.wrapS)
        sampling.tAddressMode = address(mesh.wrapT)
        sampling.magFilter = mesh.magFilter == 9728 ? .nearest : .linear
        sampling.minFilter = [9728, 9984, 9986].contains(mesh.minFilter) ? .nearest : .linear
        sampling.mipFilter =
          [9984, 9985].contains(mesh.minFilter)
          ? .nearest : [9986, 9987].contains(mesh.minFilter) ? .linear : .notMipmapped
        guard let sampler = device.makeSamplerState(descriptor: sampling) else {
          exceeded = true
          continue
        }
        sceneResources[mesh.id] = .init(
          vertex: vertex, indices: index, texture: texture, count: Int(mesh.indexCount),
          sampler: sampler,
          origin: .init(mesh.origin.x, mesh.origin.y, mesh.origin.z),
          color: .init(mesh.red, mesh.green, mesh.blue, mesh.alpha), cutoff: mesh.alphaCutoff,
          doubleSided: mesh.doubleSided != 0, blend: mesh.blend != 0)
        bytes += vertex.allocatedSize + index.allocatedSize
      }
      visible.append(mesh.id)
    }
    let changed = visible != visibleMeshes
    visibleMeshes = visible
    nativeScene.renderedMeshes(visible)
    let kept = Set(visible)
    sceneResources = sceneResources.filter { kept.contains($0.key) }
    sceneTextures = sceneTextures.filter { textureIDs.contains($0.key) }
    let status =
      "3D Tiles: \(visible.count) meshes · \(Int(nativeScene.progress))% loaded"
      + (nativeScene.failures > 0 ? " · \(nativeScene.failures) load/format failures" : "")
      + (exceeded ? " · GPU memory limit reached" : "")
    sceneStatus = status
    let sourceCredit =
      tilesetURL?.absoluteString
        == "https://pelican-public.s3.amazonaws.com/3dtiles/agi-hq/tileset.json"
      ? "AGI headquarters photogrammetry · public Cesium 3D Tiles resource" : ""
    onSceneStatus?(
      status, [sourceCredit, nativeScene.credits].filter { !$0.isEmpty }.joined(separator: " · "))
    if changed {
      cachedCenter = nil
      view.needsDisplay = true
      onSurfaceUpdated?()
    }
    return changed
  }
  func focusScene() -> Camera? {
    guard let (center, radius) = nativeScene?.center, let point = SurfacePoint(ecef: center) else {
      return nil
    }
    return Camera(center: point.coordinate, altitude: max(500, radius * 3), heading: 0, tilt: 55)
  }
  private func drawScene(encoder: any MTLRenderCommandEncoder, uniforms: inout Uniforms) {
    encoder.setRenderPipelineState(meshPipeline)
    let resources = visibleMeshes.compactMap { sceneResources[$0] }.sorted {
      if $0.blend != $1.blend { return !$0.blend }
      return simd_distance($0.origin, pose.eye) > simd_distance($1.origin, pose.eye)
    }
    for mesh in resources {
      encoder.setDepthStencilState(mesh.blend ? overlayDepth : depth)
      encoder.setCullMode(mesh.doubleSided ? .none : .back)
      encoder.setFrontFacing(.counterClockwise)
      uniforms.offset = SIMD4(SIMD3<Float>((mesh.origin - pose.target) / 6_378_137), 0)
      uniforms.color = mesh.color
      uniforms.settings = SIMD4(mesh.cutoff, 0, 0, mesh.blend ? 1 : 0)
      encoder.setVertexBytes(&uniforms, length: MemoryLayout<Uniforms>.stride, index: 1)
      encoder.setFragmentBytes(&uniforms, length: MemoryLayout<Uniforms>.stride, index: 1)
      encoder.setVertexBuffer(mesh.vertex, offset: 0, index: 0)
      encoder.setFragmentTexture(mesh.texture, index: 0)
      encoder.setFragmentSamplerState(mesh.sampler, index: 0)
      encoder.drawIndexedPrimitives(
        type: .triangle, indexCount: mesh.count, indexType: .uint32, indexBuffer: mesh.indices,
        indexBufferOffset: 0)
    }
    encoder.setCullMode(.none)
    encoder.setDepthStencilState(depth)
  }

  func nearestRecord(to location: CGPoint, size: CGSize) -> GeoRecord? {
    let transform = matrix(size: size)
    let eye = pose.eye / 6_378_137
    var result: GeoRecord?
    var distance = 18.0
    for record in records {
      let world =
        record.coordinate.ecef(
          altitude: max(
            0,
            record.altitude
              ?? (terrainEnabled ? renderedGroundHeight(at: record.coordinate) ?? 0 : 0)) + 50)
        / 6_378_137
      guard GlobeVisibility.visible(from: eye * 6_378_137, to: world * 6_378_137) else { continue }
      let relative = world - pose.target / 6_378_137
      let point = transform * SIMD4(Float(relative.x), Float(relative.y), Float(relative.z), 1)
      guard point.w > 0, point.z >= 0, point.z <= point.w else { continue }
      let x = Double(point.x / point.w + 1) * size.width / 2
      let y = Double(point.y / point.w + 1) * size.height / 2
      let d = hypot(x - location.x, y - location.y)
      if d < distance {
        if let ray = SceneRay(origin: pose.eye, direction: world * 6_378_137 - pose.eye),
          let hit = surfacePick(ray),
          simd_distance(hit, pose.eye) + 80 < simd_distance(world * 6_378_137, pose.eye)
        {
          continue
        }
        distance = d
        result = record
      }
    }
    return result
  }
  func draw(in view: MTKView) {
    guard !suspended, let pass = view.currentRenderPassDescriptor,
      let drawable = view.currentDrawable, let command = queue.makeCommandBuffer(),
      let encoder = command.makeRenderCommandEncoder(descriptor: pass)
    else { return }
    var uniforms = Uniforms(
      viewProjection: matrix(size: view.drawableSize), color: SIMD4(0.10, 0.16, 0.19, 1),
      settings: SIMD4(10, 0, 0, 0),
      eye: SIMD4(
        SIMD3<Float>(pose.eye / 6_378_137),
        Float(DisplayStyle.allCases.firstIndex(of: style) ?? 0)))
    uniforms.offset = SIMD4(SIMD3<Float>(-pose.target / 6_378_137), 0)
    encoder.setDepthStencilState(depth)
    encoder.setVertexBytes(&uniforms, length: MemoryLayout<Uniforms>.stride, index: 1)
    encoder.setFragmentBytes(&uniforms, length: MemoryLayout<Uniforms>.stride, index: 1)
    encoder.setRenderPipelineState(solid)
    encoder.setVertexBuffer(base, offset: 0, index: 0)
    encoder.drawPrimitives(type: .triangle, vertexStart: 0, vertexCount: baseCount)
    encoder.setRenderPipelineState(textured)
    let baseTiles = tiles.filter { $0.key.imagery == .off }
    for (key, tile) in baseTiles.sorted(by: {
      $0.key.tile.zoom < $1.key.tile.zoom
    }) {
      uniforms.offset = SIMD4(SIMD3<Float>((tile.origin - pose.target) / 6_378_137), 0)
      let masks = coverageMasks(parent: key.tile, loaded: Array(baseTiles.keys).map(\.tile))
      uniforms.settings.z = Float(masks.count)
      encoder.setFragmentBytes(
        masks.isEmpty ? [SIMD4<Float>(0, 0, 0, 0)] : masks,
        length: max(1, masks.count) * MemoryLayout<SIMD4<Float>>.stride, index: 2)
      encoder.setVertexBytes(&uniforms, length: MemoryLayout<Uniforms>.stride, index: 1)
      encoder.setFragmentBytes(&uniforms, length: MemoryLayout<Uniforms>.stride, index: 1)
      encoder.setVertexBuffer(tile.vertex, offset: 0, index: 0)
      encoder.setFragmentTexture(tile.texture, index: 0)
      encoder.drawPrimitives(type: .triangle, vertexStart: 0, vertexCount: tile.count)
    }
    if dailyImagery != .off {
      encoder.setDepthStencilState(overlayDepth)
      encoder.setFrontFacing(.counterClockwise)
      encoder.setCullMode(.back)
      uniforms.color = SIMD4(1, 1, 1, Float(imageryOpacity))
      encoder.setVertexBytes(&uniforms, length: MemoryLayout<Uniforms>.stride, index: 1)
      encoder.setFragmentBytes(&uniforms, length: MemoryLayout<Uniforms>.stride, index: 1)
      for (key, tile) in tiles where key.imagery == .off {
        let parentZoom = min(9, key.tile.zoom)
        let scale = 1 << (key.tile.zoom - parentZoom)
        guard let parent = TileID(x: key.tile.x / scale, y: key.tile.y / scale, zoom: parentZoom),
          let overlay = tiles[RasterKey(tile: parent, imagery: dailyImagery, date: imageryDate)]
        else { continue }
        uniforms.offset = SIMD4(SIMD3<Float>((tile.origin - pose.target) / 6_378_137), 0)
        uniforms.texTransform = SIMD4(
          1 / Float(scale), 1 / Float(scale), Float(key.tile.x % scale) / Float(scale),
          Float(key.tile.y % scale) / Float(scale))
        let masks = coverageMasks(parent: key.tile, loaded: Array(baseTiles.keys).map(\.tile))
        uniforms.settings.z = Float(masks.count)
        encoder.setFragmentBytes(
          masks.isEmpty ? [SIMD4<Float>(0, 0, 0, 0)] : masks,
          length: max(1, masks.count) * MemoryLayout<SIMD4<Float>>.stride, index: 2)
        encoder.setVertexBytes(&uniforms, length: MemoryLayout<Uniforms>.stride, index: 1)
        encoder.setFragmentBytes(&uniforms, length: MemoryLayout<Uniforms>.stride, index: 1)
        encoder.setVertexBuffer(tile.vertex, offset: 0, index: 0)
        encoder.setFragmentTexture(overlay.texture, index: 0)
        encoder.drawPrimitives(type: .triangle, vertexStart: 0, vertexCount: tile.count)
      }
      uniforms.texTransform = SIMD4(1, 1, 0, 0)

      encoder.setCullMode(.none)
      encoder.setDepthStencilState(depth)
    }
    for layer in rasterLayers where !layer.resources.isEmpty {
      let overlay = layer.overlay
      encoder.setRenderPipelineState(textured)
      encoder.setDepthStencilState(overlayDepth)
      encoder.setCullMode(.none)
      uniforms.color = SIMD4(1, 1, 1, overlay.opacity)
      uniforms.texTransform = SIMD4(1, 1, 0, 0)
      let previousStyle = uniforms.eye.w
      uniforms.eye.w = 0  // SAR colors never pass through simulated sensor effects.
      for (id, sar) in layer.resources.sorted(by: { $0.key.zoom < $1.key.zoom }) {
        let masks = coverageMasks(parent: id, loaded: Array(baseTiles.keys).map(\.tile)).map {
          rectangle in
          let a = sarUV(
            id.coordinate(u: Double(rectangle.x), v: Double(rectangle.y)), bounds: overlay.bounds)
          let b = sarUV(
            id.coordinate(u: Double(rectangle.z), v: Double(rectangle.w)), bounds: overlay.bounds)
          return SIMD4(a.x, a.y, b.x, b.y)
        }
        uniforms.settings = SIMD4(0, 0, Float(masks.count), overlay.pixelGrid ? 3 : 2)
        encoder.setFragmentBytes(
          masks.isEmpty ? [SIMD4<Float>.zero] : masks,
          length: max(1, masks.count) * MemoryLayout<SIMD4<Float>>.stride, index: 2)
        uniforms.offset = SIMD4(SIMD3<Float>((sar.origin - pose.target) / 6_378_137), 0)
        encoder.setVertexBytes(&uniforms, length: MemoryLayout<Uniforms>.stride, index: 1)
        encoder.setFragmentBytes(&uniforms, length: MemoryLayout<Uniforms>.stride, index: 1)
        encoder.setVertexBuffer(sar.vertex, offset: 0, index: 0)
        encoder.setFragmentTexture(sar.texture, index: 0)
        encoder.drawPrimitives(type: .triangle, vertexStart: 0, vertexCount: sar.count)
      }
      uniforms.eye.w = previousStyle
      encoder.setDepthStencilState(depth)
    }
    drawScene(encoder: encoder, uniforms: &uniforms)
    uniforms.offset = SIMD4(SIMD3<Float>(-pose.target / 6_378_137), 0)
    encoder.setRenderPipelineState(lines)
    encoder.setDepthStencilState(depth)
    uniforms.settings = SIMD4(
      Float(2 * view.windowScale), 0, Float(view.drawableSize.width),
      Float(view.drawableSize.height))
    for (buffer, count, color) in pathBuffers {
      uniforms.color = color
      encoder.setVertexBytes(&uniforms, length: MemoryLayout<Uniforms>.stride, index: 1)
      encoder.setVertexBuffer(buffer, offset: 0, index: 0)
      encoder.drawPrimitives(type: .triangle, vertexStart: 0, vertexCount: count)
    }
    for (buffer, count, color) in [
      (solarBuffer, solarCount, SIMD4<Float>(1, 0.78, 0.32, 1)),
      (footprintBuffer, footprintCount, SIMD4<Float>(0.5, 0.95, 0.85, 1)),
    ] {
      if let buffer, count > 1 {
        uniforms.color = color
        encoder.setVertexBytes(&uniforms, length: MemoryLayout<Uniforms>.stride, index: 1)
        encoder.setVertexBuffer(buffer, offset: 0, index: 0)
        encoder.drawPrimitives(type: .triangle, vertexStart: 0, vertexCount: count)
      }
    }
    if let markerBuffer {
      encoder.setDepthStencilState(overlayDepth)
      uniforms.settings.y = 1
      encoder.setRenderPipelineState(points)
      encoder.setVertexBuffer(markerBuffer, offset: 0, index: 0)
      for (start, count, color) in recordGroups {
        uniforms.color = color
        uniforms.settings.x = Float(10 * view.windowScale)
        encoder.setVertexBytes(&uniforms, length: MemoryLayout<Uniforms>.stride, index: 1)
        encoder.drawPrimitives(type: .point, vertexStart: start, vertexCount: count)
      }
    }
    if let record = records.first(where: { $0.id == selected }) {
      encoder.setRenderPipelineState(points)
      encoder.setDepthStencilState(overlayDepth)
      uniforms.settings.y = 1
      let p =
        record.coordinate.ecef(
          altitude: max(
            0,
            record.altitude
              ?? (terrainEnabled ? renderedGroundHeight(at: record.coordinate) ?? 0 : 0)) + 70)
        / 6_378_137
      var vertex = PackedVertex(x: Float(p.x), y: Float(p.y), z: Float(p.z), u: 0, v: 0)
      uniforms.color = SIMD4(1, 1, 1, 1)
      uniforms.settings.x = Float((record.footprint.isEmpty ? 15 : 5) * view.windowScale)
      encoder.setVertexBytes(&vertex, length: MemoryLayout<PackedVertex>.stride, index: 0)
      encoder.setVertexBytes(&uniforms, length: MemoryLayout<Uniforms>.stride, index: 1)
      encoder.drawPrimitives(type: .point, vertexStart: 0, vertexCount: 1)
    }
    encoder.endEncoding()
    command.present(drawable)
    command.commit()
    renderedFrames += 1
  }
  private static func markerColor(feed: FeedKind) -> SIMD4<Float> {
    switch feed {
    case .earthquakes, .energy: return SIMD4(0.96, 0.65, 0.28, 1)
    case .military: return SIMD4(0.96, 0.82, 0.37, 1)
    case .installations: return SIMD4(0.38, 0.88, 0.76, 1)
    default: return SIMD4(0.39, 0.77, 0.97, 1)
    }
  }
  private func lineVertices(
    _ coordinates: [Coordinate], altitude: Double, altitudes: [Double] = []
  )
    -> [PackedLineVertex]
  {
    guard coordinates.count >= 2 else { return [] }
    func segment(_ a: Coordinate, _ b: Coordinate, heightA: Double, heightB: Double)
      -> [PackedLineVertex]
    {
      let p = a.ecef(altitude: heightA) / 6_378_137
      let q = b.ecef(altitude: heightB) / 6_378_137
      func vertex(_ position: SIMD3<Double>, _ other: SIMD3<Double>, _ side: Float)
        -> PackedLineVertex
      {
        .init(
          x: Float(position.x), y: Float(position.y), z: Float(position.z), otherX: Float(other.x),
          otherY: Float(other.y), otherZ: Float(other.z), side: side)
      }
      let upperA = vertex(p, q, 1)
      let lowerA = vertex(p, q, -1)
      let upperB = vertex(q, p, -1)
      let lowerB = vertex(q, p, 1)
      return [upperA, lowerA, lowerB, lowerB, upperB, upperA]
    }
    var result: [PackedLineVertex] = []
    for index in 1..<coordinates.count {
      let a = coordinates[index - 1]
      let b = coordinates[index]
      var delta = b.longitude - a.longitude
      if delta > 180 { delta -= 360 }
      if delta < -180 { delta += 360 }
      let draped = altitudes.count != coordinates.count && terrainEnabled
      let spacing =
        draped && (terrain.groundHeight(at: a) != nil || terrain.groundHeight(at: b) != nil)
        ? 0.0007 : 0.1
      let steps = min(
        1000, max(1, Int(ceil(max(abs(delta), abs(b.latitude - a.latitude)) / spacing))))
      var previous = a
      let startHeight = altitudes.count == coordinates.count ? altitudes[index - 1] : altitude
      let endHeight = altitudes.count == coordinates.count ? altitudes[index] : altitude
      var previousHeight = startHeight + (draped ? renderedGroundHeight(at: a) ?? 0 : 0)
      for step in 1...steps {
        let t = Double(step) / Double(steps)
        var lon = a.longitude + delta * t
        if lon > 180 { lon -= 360 }
        if lon < -180 { lon += 360 }
        guard
          let next = Coordinate(
            latitude: a.latitude + (b.latitude - a.latitude) * t, longitude: lon)
        else { continue }
        let height =
          startHeight + (endHeight - startHeight) * t
          + (draped ? renderedGroundHeight(at: next) ?? 0 : 0)
        result += segment(previous, next, heightA: previousHeight, heightB: height)
        previous = next
        previousHeight = height
      }
    }
    return result
  }
  private func vertices(tile: TileID) -> [PackedVertex] {
    let divisions = terrainEnabled ? 32 : max(4, 32 >> min(3, tile.zoom / 3))
    let origin = tile.coordinate(u: 0.5, v: 0.5).ecef()
    let parent = tiles.filter {
      $0.key.imagery == .off && $0.key.tile.zoom < tile.zoom && $0.key.tile.zoom > 1
        && TileID.containing(tile.coordinate(u: 0.5, v: 0.5), zoom: $0.key.tile.zoom) == $0.key.tile
    }.max { $0.key.tile.zoom < $1.key.tile.zoom }
    let neighboring = [(-1, 0), (1, 0), (0, -1), (0, 1)].map { dx, dy in
      TileID(x: tile.x + dx, y: tile.y + dy, zoom: tile.zoom).map {
        tiles[RasterKey(tile: $0, basemap: basemap)] != nil
      } ?? false
    }
    func vertex(_ u: Double, _ v: Double) -> PackedVertex {
      let coordinate = tile.coordinate(u: u, v: v)
      var height =
        (terrainEnabled ? terrain.groundHeight(at: coordinate) ?? 0 : 0) + Double(tile.zoom + 1)
        * 0.15
      if terrainEnabled, let (parentKey, parentResource) = parent {
        let edges = [u, 1 - u, v, 1 - v]
        let distance = zip(edges, neighboring).filter { !$0.1 }.map { $0.0 }.min() ?? 1
        let blend = min(1, max(0, distance * Double(divisions) / 2))
        if blend < 1,
          let coarse = meshHeight(at: coordinate, tile: parentKey.tile, resource: parentResource)
        {
          height = coarse * (1 - blend) + height * blend
        }
      }
      let p = (coordinate.ecef(altitude: height) - origin) / 6_378_137
      return PackedVertex(x: Float(p.x), y: Float(p.y), z: Float(p.z), u: Float(u), v: Float(v))
    }
    var vertices: [PackedVertex] = []
    for y in 0..<divisions {
      for x in 0..<divisions {
        let u = Double(x) / Double(divisions)
        let v = Double(y) / Double(divisions)
        let du = 1 / Double(divisions)
        let a = vertex(u, v)
        let b = vertex(u + du, v)
        let c = vertex(u, v + du)
        let d = vertex(u + du, v + du)
        vertices += [a, c, b, b, c, d]
      }
    }
    return vertices
  }
  private static func sphereVertices() -> [PackedVertex] {
    func vertex(_ lat: Double, _ lon: Double) -> PackedVertex {
      let p = Coordinate(latitude: lat, longitude: lon)!.ecef(altitude: -40_000) / 6_378_137
      return PackedVertex(x: Float(p.x), y: Float(p.y), z: Float(p.z), u: 0, v: 0)
    }
    var result: [PackedVertex] = []
    for lat in stride(from: -90.0, to: 90.0, by: 3) {
      for lon in stride(from: -180.0, to: 180.0, by: 3) {
        let a = vertex(lat, lon)
        let b = vertex(lat + 3, lon)
        let c = vertex(lat, lon + 3)
        let d = vertex(lat + 3, lon + 3)
        result += [a, b, c, c, b, d]
      }
    }
    return result
  }
}
extension MTKView {
  fileprivate var windowScale: Double { Double(window?.backingScaleFactor ?? 1) }
}
enum NativeError: Error, LocalizedError {
  case gpuUnavailable, resourcesMissing
  var errorDescription: String? {
    switch self {
    case .gpuUnavailable: "A Metal-capable GPU is required."
    case .resourcesMissing: "The application shader resources are missing."
    }
  }
}

@MainActor final class GlobeView: MTKView {
  var owner: WorkspaceModel?
  var renderer: GlobeRenderer?
  private var mouseStart: CGPoint?
  private var moved = false
  override var acceptsFirstResponder: Bool { true }
  override func mouseDown(with event: NSEvent) {
    window?.makeFirstResponder(self)
    mouseStart = convert(event.locationInWindow, from: nil)
    moved = false
  }
  override func mouseDragged(with event: NSEvent) {
    guard let start = mouseStart else { return }
    let next = convert(event.locationInWindow, from: nil)
    if event.modifierFlags.contains(.option) {
      owner?.orient(
        heading: (owner?.camera.heading ?? 0) + (next.x - start.x) * 0.4,
        tilt: (owner?.camera.tilt ?? 0) - (next.y - start.y) * 0.3)
    } else {
      owner?.pan(dx: next.x - start.x, dy: next.y - start.y)
    }
    mouseStart = next
    moved = true
  }
  override func mouseUp(with event: NSEvent) {
    if !moved, let renderer {
      let click = convert(event.locationInWindow, from: nil)
      if let owner,
        let point = renderer.pick(location: click, size: bounds.size)?.coordinate,
        owner.mapClick(at: point)
      {
        mouseStart = nil
        return
      }
      if let point = renderer.pick(location: click, size: bounds.size) {
        owner?.surfaceSelection = point
        owner?.surfaceEvidence = renderer.pickEvidence
      }
      owner?.selectedID =
        renderer.nearestRecord(to: convert(event.locationInWindow, from: nil), size: bounds.size)?
        .id
    }
    mouseStart = nil
  }
  override func scrollWheel(with event: NSEvent) { owner?.zoom(delta: -event.scrollingDeltaY) }
  override func keyDown(with event: NSEvent) {
    switch event.keyCode {
    case 123: owner?.pan(dx: -20, dy: 0)
    case 124: owner?.pan(dx: 20, dy: 0)
    case 125: owner?.pan(dx: 0, dy: -20)
    case 126: owner?.pan(dx: 0, dy: 20)
    case 53:
      owner?.selectedID = nil
      owner?.cancelSarRegionSelection()
    default: super.keyDown(with: event)
    }
  }
}
struct SarGlobe: View {
  @ObservedObject var model: WorkspaceModel
  @ObservedObject var sar: SarWorkspace
  @ObservedObject var satellite: SatelliteWorkspace
  @ObservedObject var optical: OpticalWorkspace
  @ObservedObject var missions: MissionWorkspace
  private var overlays: [SarOverlay] {
    if missions.comparisonEnabled { return model.comparisonLayers }
    let selected: SarOverlay?
    switch model.rasterMode {
    case "Optical": selected = optical.overlay
    case "Review": selected = satellite.overlay
    case "None": selected = nil
    default: selected = sar.overlay
    }
    return selected.map { [$0] } ?? []
  }
  var body: some View {
    ZStack(alignment: .topLeading) {
      MetalGlobe(model: model, rasterOverlays: overlays)
      VStack(alignment: .leading) {
        if model.sarRegionSelecting {
          VStack(alignment: .leading, spacing: 6) {
            Text(
              model.sarRegionFirstCorner == nil
                ? "Select the first corner of your satellite region / site"
                : "Select the opposite corner of your satellite region / site")
            if let error = model.sarAcquisition.error { Text(error).foregroundStyle(.orange) }
            Button("Cancel region selection") {
              model.cancelSarRegionSelection()
              model.openPanel(.satellite)
            }
          }.font(.callout).padding(10).background(
            .regularMaterial, in: RoundedRectangle(cornerRadius: 8))
        }
        Spacer()
        if let label = model.sarOverlayStatus {
          Text(label).font(.caption).padding(8).background(
            .regularMaterial, in: RoundedRectangle(cornerRadius: 6)
          )
          .lineLimit(2).frame(maxWidth: 460, alignment: .leading)
        } else if model.sarSearchRegion != nil {
          Text(
            model.sarSceneFootprints.isEmpty
              ? "Orange outline: selected satellite / site region"
              : "Orange: search region · Cyan: selected acquisition footprint"
          )
          .font(.caption).padding(8).background(
            .regularMaterial, in: RoundedRectangle(cornerRadius: 6))
        }
      }.padding(.top, 105).padding(.horizontal, 14).padding(.bottom, 40)
    }
  }
}

struct MetalGlobe: NSViewRepresentable {
  @ObservedObject var model: WorkspaceModel
  var rasterOverlays: [SarOverlay]
  func makeNSView(context: Context) -> GlobeView {
    let view = GlobeView(frame: .zero, device: MTLCreateSystemDefaultDevice())
    view.owner = model
    do {
      let renderer = try GlobeRenderer(view: view)
      view.renderer = renderer
      model.renderer = renderer
      renderer.onStatus = { [weak model] in model?.imageryStatus = $0 }
      renderer.onSurfaceUpdated = { [weak model] in model?.enforceSurfaceClearance() }
      renderer.onTerrainStatus = { [weak model] in
        if model?.terrainStatus != $0 { model?.terrainStatus = $0 }
      }
      renderer.onSceneStatus = { [weak model] status, credits in
        if model?.sceneStatus != status { model?.sceneStatus = status }
        if model?.sceneCredits != credits { model?.sceneCredits = credits }
      }
      renderer.onViewport = { [weak model] aspect in
        Task { @MainActor [weak model] in model?.viewportAspect = aspect }
      }
      renderer.update(
        camera: model.camera, records: model.records, selected: model.selectedID,
        dailyImagery: model.dailyImagery, opacity: model.imageryOpacity, solar: model.solar,
        basemap: model.basemap, style: model.displayStyle, paths: model.scenePaths,
        terrainEnabled: model.terrainEnabled, tilesetURL: model.tilesetURL)
    } catch { model.renderError = error.localizedDescription }
    view.setAccessibilityElement(true)
    view.setAccessibilityRole(.image)
    view.setAccessibilityIdentifier("metal-globe")
    view.setAccessibilityCustomActions([
      NSAccessibilityCustomAction(name: "Zoom in") { [weak model] in
        model?.zoom(delta: 8)
        return true
      },
      NSAccessibilityCustomAction(name: "Zoom out") { [weak model] in
        model?.zoom(delta: -8)
        return true
      },
      NSAccessibilityCustomAction(name: "Full globe") { [weak model] in
        model?.resetGlobe()
        return true
      },
    ])
    view.setAccessibilityLabel(
      "Metal globe. Drag to pan; option-drag to tilt and rotate; scroll to zoom. Select a source record to inspect it."
    )
    return view
  }
  func updateNSView(_ view: GlobeView, context: Context) {
    view.renderer?.onSarStatus = { [weak model] in model?.sarOverlayStatus = $0 }
    view.renderer?.setRasterOverlays(rasterOverlays)
    view.renderer?.update(
      camera: model.camera, records: model.records, selected: model.selectedID,
      dailyImagery: model.dailyImagery, opacity: model.imageryOpacity, solar: model.solar,
      basemap: model.basemap, style: model.displayStyle, paths: model.scenePaths,
      terrainEnabled: model.terrainEnabled, tilesetURL: model.tilesetURL)
  }
  static func dismantleNSView(_ view: GlobeView, coordinator: ()) {
    view.renderer?.stop()
    view.renderer = nil
    view.owner = nil
  }
}
