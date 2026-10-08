import CScene
import Foundation
import simd

@MainActor public final class NativeTileset {
  private nonisolated(unsafe) var handle: OpaquePointer?
  public let url: URL
  public init(url: URL) throws {
    guard
      url.isFileURL
        || (url.scheme == "https" && url.host != nil && url.user == nil && url.password == nil),
      let handle = url.absoluteString.withCString({ GE_scene_create($0) })
    else { throw FeedError.coverage("Choose a local tileset.json or an HTTPS 3D Tiles URL.") }
    self.url = url
    self.handle = handle
  }
  deinit { if let handle { GE_scene_destroy(handle) } }
  public func close() {
    if let handle { GE_scene_destroy(handle) }
    handle = nil
  }
  private func point(_ p: SIMD3<Double>) -> GEPoint { .init(x: p.x, y: p.y, z: p.z) }
  public func update(pose: CameraPose, width: Double, height: Double) -> Int {
    Int(
      GE_scene_update(handle, point(pose.eye), point(pose.forward), point(pose.up), width, height))
  }
  /// Mesh buffers remain valid until the next update/close; consume synchronously.
  public func mesh(at index: Int) -> GEMesh? {
    guard index >= 0, index <= UInt32.max else { return nil }
    var mesh = GEMesh()
    return GE_scene_mesh(handle, UInt32(index), &mesh) == 1 ? mesh : nil
  }
  public func renderedMeshes(_ ids: [UInt64]) {
    ids.withUnsafeBufferPointer {
      GE_scene_rendered_meshes(handle, $0.baseAddress, UInt32($0.count))
    }
  }
  public func pick(_ ray: SceneRay) -> SIMD3<Double>? {
    var hit = GEPoint()
    guard GE_scene_pick(handle, point(ray.origin), point(ray.direction), &hit) == 1 else {
      return nil
    }
    return .init(hit.x, hit.y, hit.z)
  }
  public var progress: Float { GE_scene_progress(handle) }
  public var failures: UInt32 { GE_scene_failures(handle) }
  public var credits: String {
    let text = String(cString: GE_scene_credits(handle))
    return String(text.prefix(8000)).replacingOccurrences(
      of: "<[^>]+>", with: " ", options: .regularExpression
    )
    .replacingOccurrences(of: "&amp;", with: "&").replacingOccurrences(of: "&copy;", with: "©")
    .replacingOccurrences(of: "\\s+", with: " ", options: .regularExpression)
    .trimmingCharacters(in: .whitespacesAndNewlines)
  }
  public var center: (SIMD3<Double>, Double)? {
    var p = GEPoint()
    var radius = 0.0
    guard GE_scene_center(handle, &p, &radius) == 1 else { return nil }
    return (.init(p.x, p.y, p.z), radius)
  }
}
