import CSGP4
import Foundation

public enum SatelliteGroup: String, CaseIterable, Codable, Sendable {
  case stations, active, starlink, weather
  case gps = "gps-ops"
  public var name: String { rawValue == "gps-ops" ? "GPS" : rawValue.capitalized }
}
public struct OrbitalSatellite: @unchecked Sendable {
  public let commonName: String
  public let noradIdent: String
  public let epoch: Date
  private let parameters: GEElements
  fileprivate init(commonName: String, noradIdent: String, epoch: Date, parameters: GEElements) {
    self.commonName = commonName
    self.noradIdent = noradIdent
    self.epoch = epoch
    self.parameters = parameters
  }
  public var period: TimeInterval { 2 * Double.pi / parameters.motion * 60 }
  public func estimatedOrbit(at time: Date = Date()) throws -> ScenePath {
    var points: [Coordinate] = []
    var altitudes: [Double] = []
    for sample in 0...96 {
      let fix = try geoPosition(at: time.addingTimeInterval(period * Double(sample) / 96))
      guard let point = Coordinate(latitude: fix.lat, longitude: fix.lon), fix.alt > 0,
        fix.alt < 100_000
      else { throw FeedError.malformed }
      points.append(point)
      altitudes.append(fix.alt * 1000)
    }
    return ScenePath(points: points, altitudes: altitudes, kind: .orbit)
  }
  public func position(minsAfterEpoch: Double) throws -> SIMD3<Double> {
    var parameters = self.parameters
    var position = [Double](repeating: 0, count: 3)
    var velocity = position
    guard ge_sgp4(&parameters, minsAfterEpoch, &position, &velocity) == 1 else {
      throw FeedError.malformed
    }
    return SIMD3(position[0], position[1], position[2])
  }
  public func geoPosition(at time: Date) throws -> (lat: Double, lon: Double, alt: Double) {
    let p = try position(minsAfterEpoch: time.timeIntervalSince(epoch) / 60)
    let jd = time.timeIntervalSince1970 / 86400 + 2440587.5
    let centuries = (jd - 2_451_545) / 36525
    let seconds =
      67310.54841 + (876600 * 3600 + 8640184.812866) * centuries + 0.093104 * centuries * centuries
      - 6.2e-6 * pow(centuries, 3)
    let theta = (seconds * Double.pi / 43200).truncatingRemainder(dividingBy: 2 * Double.pi)
    let horizontal = hypot(p.x, p.y)
    let e2 = 0.0066943799901413165
    let a = 6378.137
    var latitude = atan2(p.z, horizontal)
    for _ in 0..<20 {
      let n = a / sqrt(1 - e2 * pow(sin(latitude), 2))
      latitude = atan2(p.z + n * e2 * sin(latitude), horizontal)
    }
    let n = a / sqrt(1 - e2 * pow(sin(latitude), 2))
    let altitude = horizontal > 1e-6 ? horizontal / cos(latitude) - n : abs(p.z) - a * sqrt(1 - e2)
    var longitude = (atan2(p.y, p.x) - theta) * 180 / Double.pi
    longitude = (longitude + 540).truncatingRemainder(dividingBy: 360) - 180
    return (latitude * 180 / Double.pi, longitude, altitude)
  }
}
public struct OrbitalCatalog: Sendable {
  public let satellites: [OrbitalSatellite]
  public let rejected: Int
  public let retrievedAt: Date
  public static func parse(_ data: Data, retrievedAt: Date = Date()) throws -> OrbitalCatalog {
    guard let text = String(data: data, encoding: .utf8) else { throw FeedError.malformed }
    let lines = text.components(separatedBy: .newlines).filter {
      !$0.trimmingCharacters(in: .whitespaces).isEmpty
    }
    var satellites: [OrbitalSatellite] = []
    var seen = Set<String>()
    var rejected = 0
    var index = 0
    while index < lines.count {
      let hasName = !lines[index].hasPrefix("1 ")
      let first = index + (hasName ? 1 : 0)
      guard first + 1 < lines.count else {
        rejected += 1
        break
      }
      let l1 = lines[first]
      let l2 = lines[first + 1]
      index = first + 2
      guard validLines(l1, l2),
        let elements = validatedElements(
          name: hasName ? lines[first - 1] : "", line1: l1, line2: l2)
      else {
        rejected += 1
        continue
      }
      let satellite = elements
      guard seen.insert(satellite.noradIdent).inserted else {
        rejected += 1
        continue
      }
      satellites.append(satellite)
    }
    guard !satellites.isEmpty else { throw FeedError.malformed }
    return OrbitalCatalog(satellites: satellites, rejected: rejected, retrievedAt: retrievedAt)
  }
  private static func validatedElements(name: String, line1: String, line2: String)
    -> OrbitalSatellite?
  {
    let one = Array(line1.utf8)
    let two = Array(line2.utf8)
    func text(_ bytes: [UInt8], _ range: ClosedRange<Int>) -> String {
      String(bytes: bytes[range], encoding: .utf8)!.trimmingCharacters(in: .whitespaces)
    }
    guard let id = norad(text(one, 2...6)), norad(text(two, 2...6)) == id,
      let year = Int(text(one, 18...19)), let day = Double(text(one, 20...31)), day.isFinite,
      let inclination = Double(text(two, 8...15)), (0...180).contains(inclination),
      let node = Double(text(two, 17...24)), (0..<360).contains(node),
      let eccentricity = Double("0." + text(two, 26...32).replacingOccurrences(of: " ", with: "0")),
      (0..<1).contains(eccentricity),
      let perigee = Double(text(two, 34...41)), (0..<360).contains(perigee),
      let anomaly = Double(text(two, 43...50)), (0..<360).contains(anomaly),
      let motion = Double(text(two, 52...62)), motion > 0, motion < 20,
      Int(text(one, 64...67)) != nil, Int(text(two, 63...67)) != nil,
      let exponent = Int(text(one, 59...60)),
      let coefficient = Double(
        "0."
          + String(bytes: one[54...58], encoding: .utf8)!.replacingOccurrences(of: " ", with: "0"))
    else { return nil }
    var calendar = Calendar(identifier: .gregorian)
    calendar.timeZone = TimeZone(secondsFromGMT: 0)!
    let fullYear = year < 57 ? year + 2000 : year + 1900
    guard let jan1 = calendar.date(from: DateComponents(year: fullYear, month: 1, day: 1)),
      let nextYear = calendar.date(from: DateComponents(year: fullYear + 1, month: 1, day: 1)),
      day >= 1, day < nextYear.timeIntervalSince(jan1) / 86400 + 1
    else { return nil }
    let drag = (one[53] == 45 ? -1.0 : 1.0) * coefficient * pow(10, Double(exponent))
    let epoch = jan1.addingTimeInterval((day - 1) * 86400)
    let radians = Double.pi / 180
    let parameters = GEElements(
      epoch: epoch.timeIntervalSince1970 / 86400 + 7306, bstar: drag, eccentricity: eccentricity,
      perigee: perigee * radians, inclination: inclination * radians, anomaly: anomaly * radians,
      motion: motion * 2 * Double.pi / 1440, node: node * radians)
    return OrbitalSatellite(
      commonName: SourceText.clean(name, limit: 160) ?? "NORAD \(id)", noradIdent: String(id),
      epoch: epoch, parameters: parameters)
  }
  private static func norad(_ text: String) -> UInt? {
    if let value = UInt(text) { return value }
    let alphabet = Array("ABCDEFGHJKLMNPQRSTUVWXYZ")
    guard text.count == 5, let first = text.first, let index = alphabet.firstIndex(of: first),
      let suffix = UInt(text.dropFirst())
    else { return nil }
    return UInt(index + 10) * 10000 + suffix
  }
  private static func validLines(_ a: String, _ b: String) -> Bool {
    func checksum(_ line: String, kind: UInt8) -> Bool {
      let bytes = Array(line.utf8)
      guard bytes.count == 69, bytes[0] == kind, bytes[1] == 32,
        bytes.allSatisfy({
          $0 == 32 || $0 == 43 || $0 == 45 || $0 == 46 || (48...57).contains($0)
            || (65...90).contains($0)
        }), (48...57).contains(bytes[68])
      else { return false }
      let sum = bytes.prefix(68).reduce(0) {
        $0 + ((48...57).contains($1) ? Int($1 - 48) : $1 == 45 ? 1 : 0)
      }
      return sum % 10 == Int(bytes[68] - 48)
    }
    return checksum(a, kind: 49) && checksum(b, kind: 50)
  }
  public func snapshot(at time: Date = Date()) -> Snapshot {
    var records: [GeoRecord] = []
    records.reserveCapacity(satellites.count)
    let formatter = ISO8601DateFormatter()
    let propagationUTC = formatter.string(from: time)
    var rejected = self.rejected
    for satellite in satellites {
      guard let point = try? satellite.geoPosition(at: time),
        point.alt.isFinite, point.alt > 0, point.alt < 100_000,
        let coordinate = Coordinate(latitude: point.lat, longitude: point.lon)
      else {
        rejected += 1
        continue
      }
      let epoch = satellite.epoch
      records.append(
        .init(
          id: "norad:\(satellite.noradIdent)", feed: .satellites, label: satellite.commonName,
          coordinate: coordinate, altitude: point.alt * 1000, observedAt: epoch,
          source: "CelesTrak / Vallado SGP4",
          details: [
            "NORAD": satellite.noradIdent,
            "Position basis": "SGP4/SDP4 propagated estimate, not a live observation",
            "Propagation UTC": propagationUTC,
            "TLE epoch UTC": formatter.string(from: epoch),
            "TLE age": String(format: "%.1f days", time.timeIntervalSince(epoch) / 86400),
            "Source URL": "https://celestrak.org/satcat/records.php?CATNR=\(satellite.noradIdent)",
          ]))
    }
    return Snapshot(
      records: records, rejected: rejected, source: "CelesTrak",
      notices: [
        "Positions are propagated from TLE epochs. Accuracy degrades with age; no measured position or certainty is implied."
      ], retrievedAt: retrievedAt)
  }
}
