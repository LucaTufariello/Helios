import Foundation
import XCTest

@testable import GodEyeCore

final class WorkspaceCompatibilityTests: XCTestCase {
  private func previousWorkspace(layer: String, withAnnotation: Bool) throws -> Data {
    var state = WorkspaceState()
    state.camera = Camera(center: Coordinate(latitude: 48.85, longitude: 2.35)!, altitude: 20_000)
    state.layers = [.earthquakes, .cables]
    state.basemap = .streets
    state.style = .monochrome
    state.opacity = 0.6
    state.solar = true
    if withAnnotation {
      state.annotations = [.init(title: "My saved note", points: [state.camera.center])]
    }
    var document = try XCTUnwrap(
      JSONSerialization.jsonObject(with: JSONEncoder().encode(state)) as? [String: Any])
    document["layers"] = ["earthquakes", "cables", layer]
    document["trackedID"] = "neighborhoods:Mission"
    return try JSONSerialization.data(withJSONObject: document)
  }
  func testPreviousWorkspacePreservesNotesAndSettingsWithoutRemovedLayer() throws {
    let data = try previousWorkspace(layer: "neighborhoods", withAnnotation: true)
    let state = try JSONDecoder().decode(WorkspaceState.self, from: data).validated()
    XCTAssertEqual(state.layers, [.earthquakes, .cables])
    XCTAssertEqual(state.camera.center, Coordinate(latitude: 48.85, longitude: 2.35))
    XCTAssertEqual(state.camera.altitude, 20_000)
    XCTAssertEqual(state.basemap, .streets)
    XCTAssertEqual(state.style, .monochrome)
    XCTAssertEqual(state.opacity, 0.6)
    XCTAssertTrue(state.solar)
    XCTAssertNil(state.trackedID)
    XCTAssertEqual(state.annotations.first?.title, "My saved note")
    let exported = try XCTUnwrap(
      JSONSerialization.jsonObject(with: JSONEncoder().encode(state)) as? [String: Any])
    XCTAssertEqual(Set(try XCTUnwrap(exported["layers"] as? [String])), ["earthquakes", "cables"])
    // Arbitrary source names must still fail validation rather than silently disappear.
    XCTAssertThrowsError(
      try JSONDecoder().decode(
        WorkspaceState.self,
        from: previousWorkspace(layer: "unknown-source", withAnnotation: false)))
  }
  func testPreviousViewLinkOpensWithRemainingSources() throws {
    var url = URLComponents()
    url.scheme = "godeye"
    url.host = "workspace"
    url.queryItems = [
      .init(
        name: "state",
        value: try previousWorkspace(layer: "neighborhoods", withAnnotation: false)
          .base64EncodedString())
    ]
    let restored = try XCTUnwrap(WorkspaceState.from(try XCTUnwrap(url.url)))
    XCTAssertEqual(restored.layers, [.earthquakes, .cables])
    XCTAssertNil(restored.trackedID)
    XCTAssertEqual(restored.basemap, .streets)
    let sharedAgain = try XCTUnwrap(WorkspaceState.from(try restored.shareURL()))
    XCTAssertEqual(sharedAgain.layers, restored.layers)
  }
}
