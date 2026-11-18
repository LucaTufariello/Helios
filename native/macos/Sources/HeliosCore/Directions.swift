import Foundation

public enum TravelMode: String, CaseIterable, Sendable {
  case car, foot, bike
  public var name: String { self == .car ? "Driving" : self == .foot ? "Walking" : "Cycling" }
}
public struct RouteStep: Identifiable, Sendable {
  public let id: Int
  public let instruction: String
  public let distance: Double
  public let coordinate: Coordinate
}
public struct NativeRoute: Sendable {
  public let points: [Coordinate]
  public let distance: Double
  public let duration: Double
  public let steps: [RouteStep]
  public static func parse(_ data: Data) throws -> NativeRoute {
    guard let root = try JSONSerialization.jsonObject(with: data) as? [String: Any],
      root["code"] as? String == "Ok", let route = (root["routes"] as? [[String: Any]])?.first,
      let geometry = route["geometry"] as? [String: Any],
      let distance = Parsers.jsonNumber(route["distance"]), distance >= 0, distance <= 3_000_000,
      let duration = Parsers.jsonNumber(route["duration"]), duration >= 0, duration < 2_000_000
    else { throw FeedError.coverage("No valid route was returned by the routing provider.") }
    let points = ReferenceLayers.geometry(geometry).first ?? []
    guard points.count >= 2, points.count <= 30_000 else { throw FeedError.malformed }
    var steps: [RouteStep] = []
    for leg in route["legs"] as? [[String: Any]] ?? [] {
      for step in leg["steps"] as? [[String: Any]] ?? [] {
        guard steps.count < 2000, let maneuver = step["maneuver"] as? [String: Any],
          let loc = maneuver["location"] as? [Any], loc.count == 2,
          let lon = Parsers.jsonNumber(loc[0]), let lat = Parsers.jsonNumber(loc[1]),
          let coordinate = Coordinate(latitude: lat, longitude: lon),
          let distance = Parsers.jsonNumber(step["distance"]), distance >= 0
        else { continue }
        let type = SourceText.clean(maneuver["type"]) ?? "continue"
        let modifier = SourceText.clean(maneuver["modifier"]) ?? ""
        let road = SourceText.clean(step["name"]) ?? ""
        let action: String
        switch type {
        case "depart": action = "Start"
        case "arrive": action = "Arrive at your destination"
        case "turn": action = "Turn"
        case "end of road": action = "At the end of the road, turn"
        case "fork": action = "At the fork, keep"
        case "merge": action = "Merge"
        case "roundabout", "rotary": action = "Enter the roundabout"
        case "exit roundabout", "exit rotary": action = "Exit the roundabout"
        default: action = "Continue"
        }
        let direction = modifier.replacingOccurrences(of: "slight ", with: "slightly ")
        var instruction = action
        if !direction.isEmpty, !["arrive", "roundabout", "rotary"].contains(type) {
          instruction += " " + direction
        }
        if ["roundabout", "rotary"].contains(type), let exit = Parsers.jsonNumber(maneuver["exit"]),
          exit >= 1, exit < 100
        {
          instruction += ", take exit \(Int(exit))"
        }
        if !road.isEmpty, type != "arrive" { instruction += " onto " + road }
        steps.append(
          .init(
            id: steps.count, instruction: instruction, distance: distance, coordinate: coordinate))
      }
    }
    return NativeRoute(points: points, distance: distance, duration: duration, steps: steps)
  }
}
extension Transport {
  public func route(from a: Coordinate, to b: Coordinate, mode: TravelMode) async throws
    -> NativeRoute
  {
    let delta = a.ecef() - b.ecef()
    guard sqrt(delta.x * delta.x + delta.y * delta.y + delta.z * delta.z) <= 1_000_000 else {
      throw FeedError.coverage("Choose endpoints within 1,000 km for the public routing service.")
    }
    let profile = mode == .car ? "driving" : mode.rawValue
    var url = URLComponents(
      string:
        "https://routing.openstreetmap.de/routed-\(mode.rawValue)/route/v1/\(profile)/\(a.longitude),\(a.latitude);\(b.longitude),\(b.latitude)"
    )!
    url.queryItems = [
      .init(name: "overview", value: "full"), .init(name: "geometries", value: "geojson"),
      .init(name: "steps", value: "true"),
    ]
    return try NativeRoute.parse(
      await data(url.url!, maximumBytes: 8 * 1024 * 1024, allowRedirects: false))
  }
}
