import Foundation
import GodEyeSpatial
import simd

private func spatialVector(_ point: SIMD3<Double>) -> GEVector3 {
  GEVector3(x: point.x, y: point.y, z: point.z)
}

private func swiftVector(_ point: GEVector3) -> SIMD3<Double> {
  SIMD3(point.x, point.y, point.z)
}

public struct SurfacePoint: Sendable, Equatable {
  public let coordinate: Coordinate
  public let height: Double
  public init?(ecef p: SIMD3<Double>) {
    var point = GEGeographicPoint()
    guard
      ge_wgs84_from_ecef(spatialVector(p), &point) != 0,
      let coordinate = Coordinate(latitude: point.latitude, longitude: point.longitude)
    else { return nil }
    self.coordinate = coordinate
    self.height = point.height
  }
}

public struct SceneRay: Sendable {
  public let origin: SIMD3<Double>
  public let direction: SIMD3<Double>
  public init?(origin: SIMD3<Double>, direction: SIMD3<Double>) {
    var ray = GERay()
    guard ge_ray_make(spatialVector(origin), spatialVector(direction), &ray) != 0 else {
      return nil
    }
    self.origin = swiftVector(ray.origin)
    self.direction = swiftVector(ray.direction)
  }
  public func ellipsoidDistance() -> Double? {
    var distance = 0.0
    let ray = GERay(origin: spatialVector(origin), direction: spatialVector(direction))
    return ge_ray_ellipsoid_distance(ray, &distance) != 0 ? distance : nil
  }
  public func triangle(_ a: SIMD3<Double>, _ b: SIMD3<Double>, _ c: SIMD3<Double>) -> Double? {
    var distance = 0.0
    let ray = GERay(origin: spatialVector(origin), direction: spatialVector(direction))
    return ge_ray_triangle_distance(
      ray, spatialVector(a), spatialVector(b), spatialVector(c), &distance)
      != 0 ? distance : nil
  }
}

/// One double-precision camera basis for rendering, tile selection and picking.
public struct CameraPose: Sendable {
  public let eye: SIMD3<Double>
  public let target: SIMD3<Double>
  public let forward: SIMD3<Double>
  public let right: SIMD3<Double>
  public let up: SIMD3<Double>
  public init(camera: Camera, targetHeight: Double = 0) {
    var basis = GECameraBasis()
    let valid =
      ge_camera_basis(
        camera.center.latitude, camera.center.longitude, camera.altitude,
        camera.heading, camera.tilt, targetHeight, &basis) != 0
    eye = valid ? swiftVector(basis.eye) : SIMD3(repeating: .nan)
    target = valid ? swiftVector(basis.target) : SIMD3(repeating: .nan)
    forward = valid ? swiftVector(basis.forward) : SIMD3(repeating: .nan)
    right = valid ? swiftVector(basis.right) : SIMD3(repeating: .nan)
    up = valid ? swiftVector(basis.up) : SIMD3(repeating: .nan)
  }
  public func ray(x: Double, y: Double, aspect: Double) -> SceneRay? {
    let basis = GECameraBasis(
      eye: spatialVector(eye), target: spatialVector(target), forward: spatialVector(forward),
      right: spatialVector(right), up: spatialVector(up))
    var ray = GERay()
    guard ge_camera_ray(basis, x, y, aspect, &ray) != 0 else { return nil }
    return SceneRay(origin: swiftVector(ray.origin), direction: swiftVector(ray.direction))
  }
  // Reverse-Z infinite-far projection avoids globe-scale depth loss at close range.
  // Coordinates are relative to target and normalized by the WGS84 semi-major axis.
  public func matrix(aspect: Double) -> simd_float4x4 {
    let e = (eye - target) / 6_378_137
    let back = -forward
    let view = simd_double4x4(
      columns: (
        SIMD4(right.x, up.x, back.x, 0), SIMD4(right.y, up.y, back.y, 0),
        SIMD4(right.z, up.z, back.z, 0),
        SIMD4(-simd_dot(right, e), -simd_dot(up, e), -simd_dot(back, e), 1)
      ))
    let y = 1 / tan(Double.pi / 8)
    let x = y / max(0.01, aspect)
    let near = 0.5 / 6_378_137
    let projection = simd_double4x4(
      columns: (SIMD4(x, 0, 0, 0), SIMD4(0, y, 0, 0), SIMD4(0, 0, 0, -1), SIMD4(0, 0, near, 0)))
    let result = projection * view
    return simd_float4x4(
      columns: (
        SIMD4<Float>(result.columns.0), SIMD4<Float>(result.columns.1),
        SIMD4<Float>(result.columns.2), SIMD4<Float>(result.columns.3)
      ))
  }
}
