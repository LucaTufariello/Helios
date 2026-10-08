import Foundation
import simd

/// A bounded, non-dateline query covering the rendered viewport, not a guessed city box.
public struct GeoBounds: Hashable, Sendable {
  public let south: Double
  public let west: Double
  public let north: Double
  public let east: Double
  public init?(south: Double, west: Double, north: Double, east: Double) {
    guard Coordinate(latitude: south, longitude: west) != nil,
      Coordinate(latitude: north, longitude: east) != nil, south < north, west < east
    else { return nil }
    self.south = south
    self.west = west
    self.north = north
    self.east = east
  }
  public var query: String { [south, west, north, east].map { String($0) }.joined(separator: ",") }
  public func contains(_ point: Coordinate) -> Bool {
    (south...north).contains(point.latitude) && (west...east).contains(point.longitude)
  }
  public static func viewport(camera: Camera, aspect: Double, maximumSpan: Double) throws
    -> GeoBounds
  {
    guard aspect.isFinite, aspect > 0, aspect <= 10, maximumSpan > 0 else {
      throw FeedError.malformed
    }
    var points: [Coordinate] = []
    for y in [-1.0, 0, 1] {
      for x in [-1.0, 0, 1] {
        guard let point = surface(camera: camera, x: x, y: y, aspect: aspect) else {
          throw FeedError.coverage("Zoom in: this source needs a local view entirely on the globe.")
        }
        points.append(point)
      }
    }
    guard
      let box = GeoBounds(
        south: points.map(\.latitude).min()!, west: points.map(\.longitude).min()!,
        north: points.map(\.latitude).max()!, east: points.map(\.longitude).max()!),
      box.north - box.south <= maximumSpan, box.east - box.west <= maximumSpan
    else {
      throw FeedError.coverage(
        "Zoom in: maximum query span is \(Int(maximumSpan))°; dateline-spanning views are unsupported."
      )
    }
    return box
  }
  /// Matches the Metal renderer's 45-degree vertical field of view and ellipsoidal camera.
  public static func surface(camera: Camera, x: Double, y: Double, aspect: Double) -> Coordinate? {
    guard let ray = CameraPose(camera: camera).ray(x: x, y: y, aspect: aspect),
      let distance = ray.ellipsoidDistance()
    else { return nil }
    return SurfacePoint(ecef: ray.origin + distance * ray.direction)?.coordinate
  }
}

public struct SolarState: Equatable, Sendable {
  public let date: Date
  public let subsolar: Coordinate
  public init(date: Date = Date()) {
    self.date = date
    var calendar = Calendar(identifier: .gregorian)
    calendar.timeZone = TimeZone(secondsFromGMT: 0)!
    let parts = calendar.dateComponents([.hour, .minute, .second], from: date)
    let minutes =
      Double(parts.hour ?? 0) * 60 + Double(parts.minute ?? 0) + Double(parts.second ?? 0) / 60
    let day = Double(calendar.ordinality(of: .day, in: .year, for: date) ?? 1) + minutes / 1440
    let latitude = -23.44 * cos(2 * .pi * (day + 10) / 365)
    let longitude = 180 - minutes * 0.25
    subsolar = Coordinate(latitude: latitude, longitude: longitude)!
  }
  public var utcDescription: String { ISO8601DateFormatter().string(from: date) }
  public func elevation(at point: Coordinate) -> Double {
    let lat = point.latitude * .pi / 180
    let decl = subsolar.latitude * .pi / 180
    let hour = (point.longitude - subsolar.longitude) * .pi / 180
    let sine = sin(lat) * sin(decl) + cos(lat) * cos(decl) * cos(hour)
    return asin(min(1, max(-1, sine))) * 180 / .pi
  }
  /// Orthogonal great circle remains well-defined at equinox and at the date line.
  public var terminator: [Coordinate] {
    let lat = subsolar.latitude * .pi / 180
    let lon = subsolar.longitude * .pi / 180
    let n = SIMD3<Double>(cos(lat) * cos(lon), cos(lat) * sin(lon), sin(lat))
    let u = simd_normalize(simd_cross(n, SIMD3<Double>(0, 0, 1)))
    let v = simd_cross(n, u)
    return (0...720).map { index in
      let angle = Double(index) * 2 * .pi / 720
      let p = u * cos(angle) + v * sin(angle)
      return Coordinate(
        latitude: asin(min(1, max(-1, p.z))) * 180 / .pi,
        longitude: atan2(p.y, p.x) * 180 / .pi)!
    }
  }
}

/// Visibility against the WGS84 ellipsoid, including objects above the local horizon.
public enum GlobeVisibility {
  public static func visible(from eye: SIMD3<Double>, to point: SIMD3<Double>) -> Bool {
    let radii = SIMD3<Double>(6_378_137, 6_378_137, 6_356_752.314245)
    let origin = eye / radii
    let direction = point / radii - origin
    let lengthSquared = simd_dot(direction, direction)
    guard lengthSquared.isFinite, lengthSquared > 0 else { return false }
    let closest = -simd_dot(origin, direction) / lengthSquared
    if closest <= 0 || closest >= 1 { return true }
    return simd_length_squared(origin + closest * direction) >= 1
  }
}
