import Foundation
import XCTest

@testable import HeliosCore

final class SatelliteDeletionTests: XCTestCase {
  private func temporaryRoot() throws -> URL {
    let root = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
    try FileManager.default.createDirectory(at: root, withIntermediateDirectories: true)
    addTeardownBlock { try? FileManager.default.removeItem(at: root) }
    return root
  }
  private func opticalImage(id: UUID, root: URL) throws -> URL {
    let fixture = try XCTUnwrap(
      Bundle.module.url(
        forResource: "sentinel2-result", withExtension: "json", subdirectory: "Fixtures"))
    let directory = root.appendingPathComponent(id.uuidString)
    try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
    try FileManager.default.copyItem(
      at: fixture, to: directory.appendingPathComponent("result.json"))
    try Data("generated preview".utf8).write(to: directory.appendingPathComponent("preview.png"))
    return directory
  }
  func testDeleteSitePersistsAndPreservesOtherSitesAndReviewEvidence() throws {
    let root = try temporaryRoot()
    let file = root.appendingPathComponent("sites.json")
    let region = try SentinelRegion(west: 9, south: 45, east: 9.01, north: 45.01)
    let first = InfrastructureSite(name: "First", kind: "Infrastructure", region: region)
    let second = InfrastructureSite(name: "Second", kind: "Infrastructure", region: region)
    try SatelliteDocuments.saveSites([first, second], to: file)
    let evidence = root.appendingPathComponent("reviews/evidence.json")
    try SatelliteDocuments.save(first, to: evidence)
    let original = try Data(contentsOf: evidence)
    XCTAssertEqual(try SatelliteDocuments.deleteSite(id: first.id, from: file), [second])
    XCTAssertEqual(try SatelliteDocuments.loadSites(from: file), [second])
    XCTAssertEqual(try Data(contentsOf: evidence), original)
    XCTAssertEqual(try SatelliteDocuments.deleteSite(id: second.id, from: file), [])
    XCTAssertTrue(try SatelliteDocuments.loadSites(from: file).isEmpty)
  }
  func testDeleteSiteRejectsMissingIDAndCorruptStorageWithoutWriting() throws {
    let file = try temporaryRoot().appendingPathComponent("sites.json")
    try SatelliteDocuments.saveSites([], to: file)
    let original = try Data(contentsOf: file)
    XCTAssertThrowsError(try SatelliteDocuments.deleteSite(id: UUID(), from: file))
    XCTAssertEqual(try Data(contentsOf: file), original)
    let corrupt = Data("corrupt".utf8)
    try corrupt.write(to: file)
    XCTAssertThrowsError(try SatelliteDocuments.deleteSite(id: UUID(), from: file))
    XCTAssertEqual(try Data(contentsOf: file), corrupt)
  }
  func testTrashMovesOnlySelectedImageAndPreservesImportedProduct() throws {
    let root = try temporaryRoot()
    let library = root.appendingPathComponent("optical")
    let id = UUID()
    let selected = try opticalImage(id: id, root: library)
    let other = try opticalImage(id: UUID(), root: library)
    let input = root.appendingPathComponent("PRS_L2D_original.he5")
    try Data("original input".utf8).write(to: input)
    let trash = root.appendingPathComponent("TestTrash")
    try SatelliteDocuments.trashOpticalImage(id: id, root: library) { directory in
      XCTAssertEqual(directory.standardizedFileURL, selected.standardizedFileURL)
      try FileManager.default.moveItem(at: directory, to: trash)
    }
    XCTAssertFalse(FileManager.default.fileExists(atPath: selected.path))
    XCTAssertTrue(
      FileManager.default.fileExists(atPath: trash.appendingPathComponent("preview.png").path))
    XCTAssertTrue(FileManager.default.fileExists(atPath: other.path))
    XCTAssertEqual(try Data(contentsOf: input), Data("original input".utf8))
  }
  func testTrashFailurePreservesSavedImage() throws {
    let root = try temporaryRoot()
    let id = UUID()
    let image = try opticalImage(id: id, root: root)
    let original = try Data(contentsOf: image.appendingPathComponent("result.json"))
    XCTAssertThrowsError(
      try SatelliteDocuments.trashOpticalImage(id: id, root: root) { _ in
        throw CocoaError(.fileWriteNoPermission)
      })
    XCTAssertEqual(try Data(contentsOf: image.appendingPathComponent("result.json")), original)
    XCTAssertTrue(
      FileManager.default.fileExists(atPath: image.appendingPathComponent("preview.png").path))
  }
  func testTrashRejectsSymlinkAndActiveJobBeforeMoving() throws {
    let root = try temporaryRoot()
    let library = root.appendingPathComponent("optical")
    let id = UUID()
    let image = try opticalImage(id: id, root: library)
    let linkedID = UUID()
    try FileManager.default.createSymbolicLink(
      at: library.appendingPathComponent(linkedID.uuidString),
      withDestinationURL: image)
    var moved = false
    XCTAssertThrowsError(
      try SatelliteDocuments.trashOpticalImage(id: linkedID, root: library) { _ in moved = true })
    var result = try SarProcessor.read(
      SentinelOpticalResult.self, from: image.appendingPathComponent("result.json"))
    result.state = .processing
    try SatelliteDocuments.save(result, to: image.appendingPathComponent("result.json"))
    XCTAssertThrowsError(
      try SatelliteDocuments.trashOpticalImage(id: id, root: library) { _ in moved = true })
    XCTAssertFalse(moved)
    XCTAssertTrue(FileManager.default.fileExists(atPath: image.path))
  }
}
