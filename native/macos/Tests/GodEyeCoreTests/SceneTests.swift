import Foundation
import ImageIO
import Metal
import XCTest
import simd

@testable import GodEyeCore

final class SceneTests: XCTestCase {
  func testECEFRoundTripAtSurfacePoleAndOrbit() throws {
    for (lat, lon, height) in [
      (0.0, 0.0, 0.0), (90, 123, 1800), (-89, -179.9, 420_000), (45.4, 9.2, -430),
      (38, -75, 35_786_000),
    ] {
      let original = Coordinate(latitude: lat, longitude: lon)!
      let point = try XCTUnwrap(SurfacePoint(ecef: original.ecef(altitude: height)))
      XCTAssertEqual(point.coordinate.latitude, lat, accuracy: 1e-8)
      XCTAssertEqual(point.height, height, accuracy: 0.001)
      if abs(lat) < 90 { XCTAssertEqual(point.coordinate.longitude, lon, accuracy: 1e-8) }
    }
  }
  func testTiltedProjectionAndPickingRaysAgreeWithReverseDepth() throws {
    for tilt in [0.0, 55, 80] {
      let pose = CameraPose(
        camera: Camera(
          center: .init(latitude: 45, longitude: 9)!, altitude: 800, heading: 123, tilt: tilt),
        targetHeight: 2100)
      for (x, y) in [(-0.8, 0.7), (0, 0), (0.9, -0.8)] {
        let ray = try XCTUnwrap(pose.ray(x: x, y: y, aspect: 1.8))
        let world = ray.origin + ray.direction * 200
        let clip =
          pose.matrix(aspect: 1.8) * SIMD4(SIMD3<Float>((world - pose.target) / 6_378_137), 1)
        XCTAssertEqual(Double(clip.x / clip.w), x, accuracy: 0.00001)
        XCTAssertEqual(Double(clip.y / clip.w), y, accuracy: 0.00001)
        XCTAssertGreaterThan(clip.z / clip.w, 0)
        XCTAssertLessThan(clip.z / clip.w, 1)
      }
      func depth(_ distance: Double) -> Float {
        let p = pose.eye + pose.forward * distance
        let clip = pose.matrix(aspect: 1) * SIMD4(SIMD3<Float>((p - pose.target) / 6_378_137), 1)
        return clip.z / clip.w
      }
      XCTAssertGreaterThan(depth(1), depth(10))
      XCTAssertGreaterThan(depth(10), depth(1_000_000))
    }
  }
  func testCollisionUsesDisplacedEyeAndPreservesOrientation() throws {
    var camera = Camera(
      center: .init(latitude: 0, longitude: 0)!, altitude: 500, heading: 90, tilt: 80)
    camera.ensureClearance(targetHeight: 0) { point in abs(point.longitude) > 0.0001 ? 600 : 0 }
    let eye = try XCTUnwrap(SurfacePoint(ecef: CameraPose(camera: camera).eye))
    XCTAssertGreaterThanOrEqual(eye.height, 640)
    XCTAssertEqual(camera.heading, 90)
    XCTAssertEqual(camera.tilt, 80)
    XCTAssertGreaterThan(camera.altitude, 500)
    let adjusted = camera
    camera.ensureClearance(targetHeight: 0) { _ in nil }
    XCTAssertEqual(camera, adjusted)
  }
  func testOrientationSurvivesOldAndNewSavedViewsAndLinks() throws {
    let camera = Camera(center: Camera.initial.center, altitude: 500, heading: 370, tilt: 90)
    XCTAssertEqual(camera.heading, 10)
    XCTAssertEqual(camera.tilt, 80)
    XCTAssertEqual(Camera.fromShareURL(camera.shareURL), camera)
    XCTAssertEqual(
      try JSONDecoder().decode(Camera.self, from: JSONEncoder().encode(camera)), camera)
    let old = try JSONDecoder().decode(
      Camera.self, from: Data(#"{"center":{"latitude":0,"longitude":0},"altitude":500}"#.utf8))
    XCTAssertEqual(old.tilt, 0)
    XCTAssertEqual(old.heading, 0)
    XCTAssertNil(Camera.fromShareURL(URL(string: "godeye://view?lat=0&lon=0&alt=500&tilt=90")!))
  }
  func testTiltedTileCoverageContainsEyeAndVisibleFootprintAcrossDateLine() throws {
    for lon in [9.0, 179.999, -179.999] {
      let camera = Camera(
        center: .init(latitude: 40, longitude: lon)!, altitude: 3000, heading: 73, tilt: 65)
      let tiles = TileID.visible(camera: camera, aspect: 2.4)
      let zoom = try XCTUnwrap(tiles.first?.zoom)
      XCTAssertLessThanOrEqual(tiles.count, 81)
      let pose = CameraPose(camera: camera)
      for x in [-1.0, 0, 1] {
        for y in [-1.0, 0, 1] {
          if let ray = pose.ray(x: x, y: y, aspect: 2.4), let d = ray.ellipsoidDistance(),
            let p = SurfacePoint(ecef: ray.origin + ray.direction * d)
          {
            XCTAssertTrue(tiles.contains(TileID.containing(p.coordinate, zoom: zoom)))
          }
        }
      }
      XCTAssertTrue(
        tiles.contains(
          TileID.containing(try XCTUnwrap(SurfacePoint(ecef: pose.eye)).coordinate, zoom: zoom)))
    }
  }
  private func terrarium(_ bytes: [UInt8], width: Int = 256) throws -> Data {
    let provider = try XCTUnwrap(CGDataProvider(data: Data(bytes) as CFData))
    let image = try XCTUnwrap(
      CGImage(
        width: width, height: 256, bitsPerComponent: 8, bitsPerPixel: 24, bytesPerRow: width * 3,
        space: CGColorSpaceCreateDeviceRGB(),
        bitmapInfo: CGBitmapInfo(rawValue: CGImageAlphaInfo.none.rawValue), provider: provider,
        decode: nil, shouldInterpolate: false, intent: .defaultIntent))
    let data = NSMutableData()
    let destination = try XCTUnwrap(
      CGImageDestinationCreateWithData(data, "public.png" as CFString, 1, nil))
    CGImageDestinationAddImage(destination, image, nil)
    XCTAssertTrue(CGImageDestinationFinalize(destination))
    return data as Data
  }
  func testTerrariumChannelValuesAndFractionalElevationsAreNotColorConverted() throws {
    let samples: [[UInt8]] = [[128, 0, 0], [128, 1, 128], [127, 255, 192], [155, 10, 64]]
    var bytes: [UInt8] = []
    for _ in 0..<16_384 { bytes += samples.flatMap { $0 } }
    let id = TileID(x: 0, y: 0, zoom: 1)!
    let tile = try ElevationTile.decode(terrarium(bytes), id: id)
    for (i, expected) in [0.0, 1.5, -0.25, 6922.25].enumerated() {
      XCTAssertEqual(Double(tile.heights[i]), expected, accuracy: 0.000001)
    }
    XCTAssertEqual(tile.height(u: 1.0 / 256, v: 0.5 / 256), 0.75, accuracy: 0.000001)
    XCTAssertThrowsError(
      try ElevationTile.decode(Data(repeating: 0, count: 1024 * 1024 + 1), id: id))
    XCTAssertThrowsError(
      try ElevationTile.decode(
        terrarium(Array(repeating: 128, count: 255 * 256 * 3), width: 255), id: id))
  }
  func testTerrainPickingAndUnknownHeightsAreExplicit() throws {
    let coordinate = Coordinate(latitude: 0.003, longitude: 0.004)!
    let id = TileID.containing(coordinate, zoom: 14)
    var terrain = TerrainSurface()
    XCTAssertNil(terrain.height(at: coordinate))
    terrain.insert(try .init(id: id, heights: Array(repeating: 2300, count: 65_536)))
    let ray = try XCTUnwrap(
      SceneRay(
        origin: coordinate.ecef(altitude: 5000), direction: -simd_normalize(coordinate.ecef())))
    let hit = try XCTUnwrap(terrain.pick(ray))
    XCTAssertEqual(try XCTUnwrap(SurfacePoint(ecef: hit)).height, 2300, accuracy: 0.1)
    XCTAssertLessThan(simd_distance(ray.origin, hit), try XCTUnwrap(ray.ellipsoidDistance()))
    terrain.retain([])
    XCTAssertNil(terrain.height(at: coordinate))
  }

  // Generated geometry exists only in automated tests; the application has no mock scenery.
  private func localTileset(
    content: String = "roof.glb", mode: Int = 4, alphaMode: String = "OPAQUE", alpha: Double = 1
  ) throws -> URL {
    let directory = FileManager.default.temporaryDirectory.appendingPathComponent(
      "GodEyeSceneTests-\(UUID())")
    try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
    var binary = Data()
    for value: Float in [-50, -50, 0, 50, -50, 0, 0, 50, 0] {
      var bits = value.bitPattern.littleEndian
      withUnsafeBytes(of: &bits) { binary.append(contentsOf: $0) }
    }
    let model: [String: Any] = [
      "asset": ["version": "2.0", "copyright": "GodEye automated test geometry"],
      "buffers": [["byteLength": binary.count]],
      "bufferViews": [["buffer": 0, "byteOffset": 0, "byteLength": binary.count]],
      "accessors": [
        [
          "bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3", "min": [-50, -50, 0],
          "max": [50, 50, 0],
        ]
      ],
      "materials": [
        [
          "doubleSided": true, "alphaMode": alphaMode, "alphaCutoff": 0.5,
          "pbrMetallicRoughness": ["baseColorFactor": [0.2, 0.6, 0.9, alpha]],
        ]
      ],
      "meshes": [["primitives": [["attributes": ["POSITION": 0], "material": 0, "mode": mode]]]],
      "nodes": [["mesh": 0]], "scenes": [["nodes": [0]]], "scene": 0,
    ]
    var json = try JSONSerialization.data(withJSONObject: model)
    while json.count % 4 != 0 { json.append(32) }
    var glb = Data()
    func append(_ value: UInt32) {
      var little = value.littleEndian
      withUnsafeBytes(of: &little) { glb.append(contentsOf: $0) }
    }
    append(0x4654_6c67)
    append(2)
    append(UInt32(28 + json.count + binary.count))
    append(UInt32(json.count))
    append(0x4e4f_534a)
    glb.append(json)
    append(UInt32(binary.count))
    append(0x004e_4942)
    glb.append(binary)
    try glb.write(to: directory.appendingPathComponent("roof.glb"))
    let tileset: [String: Any] = [
      "asset": ["version": "1.1", "gltfUpAxis": "Z"], "geometricError": 0,
      "root": [
        "boundingVolume": ["box": [0, 0, 0, 100, 0, 0, 0, 100, 0, 0, 0, 50]], "geometricError": 0,
        "refine": "REPLACE", "transform": [0, 1, 0, 0, 0, 0, 1, 0, 1, 0, 0, 0, 6_378_237, 0, 0, 1],
        "content": ["uri": content],
      ],
    ]
    let url = directory.appendingPathComponent("tileset.json")
    try JSONSerialization.data(withJSONObject: tileset).write(to: url)
    return url
  }
  @MainActor private func load(_ scene: NativeTileset, pose: CameraPose, requireMesh: Bool = true)
    async throws -> Int
  {
    var count = 0
    for _ in 0..<300 {
      count = scene.update(pose: pose, width: 800, height: 600)
      if requireMesh ? count > 0 : scene.failures > 0 { return count }
      try await Task.sleep(for: .milliseconds(10))
    }
    return count
  }
  @MainActor func testNativeTilesetLoadsGLBTransformsAndPicksRenderedGeometry() async throws {
    let url = try localTileset()
    defer { try? FileManager.default.removeItem(at: url.deletingLastPathComponent()) }
    let scene = try NativeTileset(url: url)
    defer { scene.close() }
    let pose = CameraPose(
      camera: Camera(center: .init(latitude: 0, longitude: 0)!, altitude: 500), targetHeight: 100)
    let count = try await load(scene, pose: pose)
    XCTAssertEqual(count, 1)
    let mesh = try XCTUnwrap(scene.mesh(at: 0))
    XCTAssertEqual(mesh.vertexCount, 3)
    XCTAssertEqual(mesh.indexCount, 3)
    XCTAssertEqual(mesh.origin.x, 6_378_237, accuracy: 0.001)
    XCTAssertEqual(mesh.red, 0.2, accuracy: 0.001)
    XCTAssertEqual(scene.failures, 0)
    XCTAssertEqual(scene.progress, 100)
    let hit = try XCTUnwrap(scene.pick(try XCTUnwrap(pose.ray(x: 0, y: 0, aspect: 4.0 / 3))))
    XCTAssertEqual(try XCTUnwrap(SurfacePoint(ecef: hit)).height, 100, accuracy: 0.001)
    XCTAssertTrue(scene.credits.contains("GodEye automated test geometry"))
    scene.renderedMeshes([])
    XCTAssertNil(scene.pick(try XCTUnwrap(pose.ray(x: 0, y: 0, aspect: 1))))
    scene.renderedMeshes([mesh.id])
    XCTAssertNotNil(scene.pick(try XCTUnwrap(pose.ray(x: 0, y: 0, aspect: 1))))
    let started = Date()
    scene.close()
    XCTAssertLessThan(Date().timeIntervalSince(started), 2)
    XCTAssertNil(scene.mesh(at: 0))
  }
  @MainActor func testMissingOutOfDirectoryAndUnsupportedGeometryFailWithoutFakeMeshes()
    async throws
  {
    for (content, mode) in [("missing.glb", 4), ("../roof.glb", 4), ("roof.glb", 1)] {
      let url = try localTileset(content: content, mode: mode)
      let scene = try NativeTileset(url: url)
      let pose = CameraPose(
        camera: Camera(center: .init(latitude: 0, longitude: 0)!, altitude: 500), targetHeight: 100)
      let count = try await load(scene, pose: pose, requireMesh: false)
      XCTAssertEqual(count, 0)
      XCTAssertGreaterThan(scene.failures, 0)
      XCTAssertNil(scene.pick(try XCTUnwrap(pose.ray(x: 0, y: 0, aspect: 1))))
      scene.close()
      try FileManager.default.removeItem(at: url.deletingLastPathComponent())
    }
  }
  @MainActor func testPickingRespectsTransparentMaterialAndOpaqueAlphaSemantics() async throws {
    for mode in ["OPAQUE", "MASK", "BLEND"] {
      let url = try localTileset(alphaMode: mode, alpha: 0)
      let scene = try NativeTileset(url: url)
      let pose = CameraPose(
        camera: Camera(center: .init(latitude: 0, longitude: 0)!, altitude: 500), targetHeight: 100)
      let count = try await load(scene, pose: pose)
      XCTAssertEqual(count, 1)
      let hit = scene.pick(try XCTUnwrap(pose.ray(x: 0, y: 0, aspect: 1)))
      if mode == "OPAQUE" { XCTAssertNotNil(hit) } else { XCTAssertNil(hit) }
      scene.close()
      try FileManager.default.removeItem(at: url.deletingLastPathComponent())
    }
  }
  @MainActor func testNativeSceneCancellationBeforeFirstLoadDrainsSafely() throws {
    let url = try localTileset()
    defer { try? FileManager.default.removeItem(at: url.deletingLastPathComponent()) }
    let started = Date()
    let scene = try NativeTileset(url: url)
    scene.close()
    XCTAssertLessThan(Date().timeIntervalSince(started), 2)
    XCTAssertThrowsError(try NativeTileset(url: URL(string: "http://example.com/tileset.json")!))
  }
  func testTerrainPreferenceAndTiltedCameraRoundTripInWorkspace() throws {
    var state = WorkspaceState()
    state.terrain = false
    state.camera = Camera(
      center: .init(latitude: 45, longitude: 6)!, altitude: 1200, heading: 73, tilt: 60)
    let restored = try XCTUnwrap(WorkspaceState.from(state.shareURL()))
    XCTAssertFalse(restored.terrain)
    XCTAssertEqual(restored.camera, state.camera)
    var old = try XCTUnwrap(
      JSONSerialization.jsonObject(with: JSONEncoder().encode(state)) as? [String: Any])
    old.removeValue(forKey: "terrain")
    XCTAssertTrue(
      try JSONDecoder().decode(
        WorkspaceState.self, from: JSONSerialization.data(withJSONObject: old)
      ).terrain)
  }
  func testActualMetalShaderKeepsNearestSurfaceIndependentOfDrawOrder() throws {
    let device = try XCTUnwrap(MTLCreateSystemDefaultDevice())
    let shaderURL = URL(fileURLWithPath: #filePath).deletingLastPathComponent()
      .deletingLastPathComponent().deletingLastPathComponent().appendingPathComponent(
        "Sources/GodEyeMac/Resources/Globe.metal")
    let library = try device.makeLibrary(
      source: String(contentsOf: shaderURL, encoding: .utf8), options: nil)
    let descriptor = MTLRenderPipelineDescriptor()
    descriptor.vertexFunction = library.makeFunction(name: "tileVertex")
    descriptor.fragmentFunction = library.makeFunction(name: "solidFragment")
    descriptor.colorAttachments[0].pixelFormat = .rgba8Unorm
    descriptor.depthAttachmentPixelFormat = .depth32Float
    let pipeline = try device.makeRenderPipelineState(descriptor: descriptor)
    descriptor.fragmentFunction = library.makeFunction(name: "meshFragment")
    let meshPipeline = try device.makeRenderPipelineState(descriptor: descriptor)
    let imageDescriptor = MTLTextureDescriptor.texture2DDescriptor(
      pixelFormat: .rgba8Unorm_srgb, width: 1, height: 1, mipmapped: false)
    imageDescriptor.storageMode = .shared
    let image = try XCTUnwrap(device.makeTexture(descriptor: imageDescriptor))
    var white: UInt32 = .max
    image.replace(
      region: MTLRegionMake2D(0, 0, 1, 1), mipmapLevel: 0, withBytes: &white, bytesPerRow: 4)
    let sampler = try XCTUnwrap(device.makeSamplerState(descriptor: MTLSamplerDescriptor()))
    let depthDescriptor = MTLDepthStencilDescriptor()
    depthDescriptor.depthCompareFunction = .greaterEqual
    depthDescriptor.isDepthWriteEnabled = true
    let depth = try XCTUnwrap(device.makeDepthStencilState(descriptor: depthDescriptor))
    let queue = try XCTUnwrap(device.makeCommandQueue())
    struct Vertex {
      var x: Float
      var y: Float
      var z: Float
      var u: Float = 0
      var v: Float = 0
    }
    struct Uniforms {
      var matrix: simd_float4x4
      var color: SIMD4<Float>
      var settings = SIMD4<Float>(1, 0, 0, 0)
      var eye = SIMD4<Float>(0, 0, 0, 0)
      var offset = SIMD4<Float>(0, 0, 0, 0)
      var transform = SIMD4<Float>(1, 1, 0, 0)
    }
    let pose = CameraPose(camera: Camera(center: .init(latitude: 0, longitude: 0)!, altitude: 500))
    for (distances, textured) in [
      ([10.0, 20.0], false), ([20.0, 10.0], false), ([10.0, 20.0], true), ([20.0, 10.0], true),
    ] {
      let colorDescriptor = MTLTextureDescriptor.texture2DDescriptor(
        pixelFormat: .rgba8Unorm, width: 16, height: 16, mipmapped: false)
      colorDescriptor.usage = [.renderTarget]
      colorDescriptor.storageMode = .shared
      let color = try XCTUnwrap(device.makeTexture(descriptor: colorDescriptor))
      let zDescriptor = MTLTextureDescriptor.texture2DDescriptor(
        pixelFormat: .depth32Float, width: 16, height: 16, mipmapped: false)
      zDescriptor.usage = [.renderTarget]
      zDescriptor.storageMode = .private
      let z = try XCTUnwrap(device.makeTexture(descriptor: zDescriptor))
      let pass = MTLRenderPassDescriptor()
      pass.colorAttachments[0].texture = color
      pass.colorAttachments[0].loadAction = .clear
      pass.colorAttachments[0].storeAction = .store
      pass.depthAttachment.texture = z
      pass.depthAttachment.loadAction = .clear
      pass.depthAttachment.clearDepth = 0
      let command = try XCTUnwrap(queue.makeCommandBuffer())
      let encoder = try XCTUnwrap(command.makeRenderCommandEncoder(descriptor: pass))
      encoder.setRenderPipelineState(textured ? meshPipeline : pipeline)
      encoder.setDepthStencilState(depth)
      encoder.setFragmentTexture(image, index: 0)
      encoder.setFragmentSamplerState(sampler, index: 0)
      for distance in distances {
        let center = pose.eye + pose.forward * distance
        let points = [
          center - pose.right * 8 - pose.up * 8, center + pose.right * 8 - pose.up * 8,
          center + pose.up * 8,
        ]
        let vertices = points.map { p -> Vertex in
          let q = (p - pose.target) / 6_378_137
          return .init(x: Float(q.x), y: Float(q.y), z: Float(q.z))
        }
        var uniforms = Uniforms(
          matrix: pose.matrix(aspect: 1),
          color: distance == 10 ? .init(1, 0, 0, 1) : .init(0, 0, 1, 1))
        encoder.setVertexBytes(
          vertices, length: vertices.count * MemoryLayout<Vertex>.stride, index: 0)
        encoder.setVertexBytes(&uniforms, length: MemoryLayout<Uniforms>.stride, index: 1)
        encoder.setFragmentBytes(&uniforms, length: MemoryLayout<Uniforms>.stride, index: 1)
        encoder.drawPrimitives(type: .triangle, vertexStart: 0, vertexCount: 3)
      }
      encoder.endEncoding()
      command.commit()
      command.waitUntilCompleted()
      XCTAssertNil(command.error)
      var pixel = [UInt8](repeating: 0, count: 4)
      color.getBytes(&pixel, bytesPerRow: 4, from: MTLRegionMake2D(8, 8, 1, 1), mipmapLevel: 0)
      XCTAssertEqual(pixel, [255, 0, 0, 255])
    }
  }
}

final class LiveSceneTests: XCTestCase {
  private func enabled() throws {
    guard ProcessInfo.processInfo.environment["GODEYE_LIVE_CHECKS"] == "1" else {
      throw XCTSkip("Set GODEYE_LIVE_CHECKS=1 to check real elevation and photogrammetry.")
    }
  }
  func testRealMountainElevationPNG() async throws {
    try enabled()
    let transport = Transport()
    defer { transport.session.invalidateAndCancel() }
    let coordinate = Coordinate(latitude: 45.8326, longitude: 6.8652)!
    let id = TileID.containing(coordinate, zoom: 12)
    let data = try await transport.data(ElevationTile.url(id), maximumBytes: 1024 * 1024)
    let tile = try ElevationTile.decode(data, id: id)
    XCTAssertGreaterThan(tile.heights.max() ?? 0, 4000)
    XCTAssertLessThan(tile.heights.max() ?? 10_000, 5000)
    print(
      "Real Mont Blanc terrain: \(tile.heights.min() ?? 0)…\(tile.heights.max() ?? 0) m; \(data.count) bytes"
    )
  }
  @MainActor func testRealAGIPhotogrammetryStreamsTexturedMeshes() async throws {
    try enabled()
    let scene = try NativeTileset(
      url: URL(string: "https://pelican-public.s3.amazonaws.com/3dtiles/agi-hq/tileset.json")!)
    defer { scene.close() }
    var camera = Camera(
      center: .init(latitude: 40.0383, longitude: -75.5966)!, altitude: 1500, tilt: 55)
    var count = 0
    for _ in 0..<450 {
      count = scene.update(pose: CameraPose(camera: camera), width: 1000, height: 800)
      if let (center, _) = scene.center, let point = SurfacePoint(ecef: center) {
        camera.center = point.coordinate
      }
      if count > 0 { break }
      try await Task.sleep(for: .milliseconds(100))
    }
    XCTAssertGreaterThan(count, 0)
    let mesh = try XCTUnwrap(scene.mesh(at: 0))
    XCTAssertGreaterThan(mesh.vertexCount, 0)
    XCTAssertGreaterThan(mesh.indexCount, 0)
    XCTAssertGreaterThan(mesh.width, 0)
    XCTAssertNotNil(mesh.pixels)
    XCTAssertEqual(scene.failures, 0)
    print(
      "Real AGI photogrammetry: \(count) meshes, \(mesh.vertexCount) vertices, \(mesh.width)×\(mesh.height) texture; center \(camera.center)"
    )
    let pose = CameraPose(camera: camera)
    var picked = false
    for x in [-0.5, 0, 0.5] {
      for y in [-0.5, 0, 0.5] {
        if let ray = pose.ray(x: x, y: y, aspect: 1.25), scene.pick(ray) != nil { picked = true }
      }
    }
    XCTAssertTrue(picked)
  }
}
