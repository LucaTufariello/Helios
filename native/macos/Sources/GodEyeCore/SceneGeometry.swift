import Foundation
import simd

public struct SurfacePoint: Sendable, Equatable {
  public let coordinate: Coordinate
  public let height: Double
  public init?(ecef p: SIMD3<Double>) {
    guard p.x.isFinite, p.y.isFinite, p.z.isFinite, simd_length(p) > 1 else { return nil }
    let a = 6_378_137.0
    let e2 = 0.0066943799901413165
    let horizontal = hypot(p.x, p.y)
    var latitude = atan2(p.z, horizontal * (1 - e2))
    for _ in 0..<10 {
      let n = a / sqrt(1 - e2 * pow(sin(latitude), 2))
      let next = atan2(p.z + e2 * n * sin(latitude), horizontal)
      if abs(next - latitude) < 1e-13 { break }
      latitude = next
    }
    let n = a / sqrt(1 - e2 * pow(sin(latitude), 2))
    let height = horizontal > 1e-6 ? horizontal / cos(latitude) - n : abs(p.z) - a * sqrt(1 - e2)
    guard
      let coordinate = Coordinate(
        latitude: latitude * 180 / .pi, longitude: atan2(p.y, p.x) * 180 / .pi), height.isFinite
    else { return nil }
    self.coordinate = coordinate
    self.height = height
  }
}

public struct SceneRay: Sendable {
  public let origin: SIMD3<Double>
  public let direction: SIMD3<Double>
  public init?(origin: SIMD3<Double>, direction: SIMD3<Double>) {
    guard
      [origin.x, origin.y, origin.z, direction.x, direction.y, direction.z].allSatisfy(\.isFinite),
      simd_length(direction) > 1e-12
    else { return nil }
    self.origin = origin
    self.direction = simd_normalize(direction)
  }
  public func ellipsoidDistance() -> Double? {
    let radii = SIMD3<Double>(6_378_137, 6_378_137, 6_356_752.314245)
    let o = origin / radii
    let d = direction / radii
    let a = simd_dot(d, d)
    let b = 2 * simd_dot(o, d)
    let c = simd_dot(o, o) - 1
    let disc = b * b - 4 * a * c
    guard disc >= 0 else { return nil }
    let near = (-b - sqrt(disc)) / (2 * a)
    let far = (-b + sqrt(disc)) / (2 * a)
    return near >= 0 ? near : far >= 0 ? far : nil
  }
  public func triangle(_ a: SIMD3<Double>, _ b: SIMD3<Double>, _ c: SIMD3<Double>) -> Double? {
    let edge1 = b - a
    let edge2 = c - a
    let p = simd_cross(direction, edge2)
    let determinant = simd_dot(edge1, p)
    guard abs(determinant) > 1e-10 else { return nil }
    let inverse = 1 / determinant
    let t = origin - a
    let u = simd_dot(t, p) * inverse
    guard u >= -1e-10, u <= 1 + 1e-10 else { return nil }
    let q = simd_cross(t, edge1)
    let v = simd_dot(direction, q) * inverse
    guard v >= -1e-10, u + v <= 1 + 1e-10 else { return nil }
    let distance = simd_dot(edge2, q) * inverse
    return distance >= 0 ? distance : nil
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
    let lon = camera.center.radians.x
    let lat = camera.center.radians.y
    let normal = SIMD3<Double>(cos(lat) * cos(lon), cos(lat) * sin(lon), sin(lat))
    let east = SIMD3<Double>(-sin(lon), cos(lon), 0)
    let north = simd_cross(normal, east)
    let heading = camera.heading * .pi / 180
    let tilt = camera.tilt * .pi / 180
    let horizontal = north * cos(heading) + east * sin(heading)
    target = camera.center.ecef(altitude: targetHeight)
    let back = normal * cos(tilt) - horizontal * sin(tilt)
    eye = target + back * camera.altitude
    forward = -back
    right = simd_normalize(simd_cross(forward, horizontal))
    up = simd_normalize(simd_cross(right, forward))
  }
  public func ray(x: Double, y: Double, aspect: Double) -> SceneRay? {
    guard x.isFinite, y.isFinite, aspect.isFinite, aspect > 0 else { return nil }
    return SceneRay(
      origin: eye, direction: forward + right * x * tan(.pi / 8) * aspect + up * y * tan(.pi / 8))
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
