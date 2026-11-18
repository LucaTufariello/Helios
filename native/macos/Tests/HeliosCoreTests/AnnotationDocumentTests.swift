import Foundation
import XCTest

@testable import HeliosCore

final class AnnotationDocumentTests: XCTestCase {
  private func temporary() throws -> URL {
    let directory = FileManager.default.temporaryDirectory.appendingPathComponent(
      "Helios-annotation-test-\(UUID().uuidString)")
    try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
    addTeardownBlock { try FileManager.default.removeItem(at: directory) }
    return directory
  }
  private func note() -> LocalAnnotation {
    LocalAnnotation(title: "Test note", points: [Coordinate(latitude: 0, longitude: 0)!])
  }
  func testDocumentsPersistAndCanBeReopened() throws {
    let url = try temporary().appendingPathComponent("nested/annotations.json")
    XCTAssertTrue(try AnnotationDocument.load(from: url).isEmpty)
    let note = note()
    try AnnotationDocument.save([note], to: url)
    XCTAssertEqual(try AnnotationDocument.load(from: url), [note])
  }
  func testInvalidReplacementDoesNotOverwriteSavedNotes() throws {
    let url = try temporary().appendingPathComponent("annotations.json")
    let note = note()
    try AnnotationDocument.save([note], to: url)
    let before = try Data(contentsOf: url)
    XCTAssertThrowsError(try AnnotationDocument.save([note, note], to: url))
    var invalid = note
    invalid.closed = true
    XCTAssertThrowsError(try AnnotationDocument.save([invalid], to: url))
    XCTAssertEqual(try Data(contentsOf: url), before)
  }
  func testCorruptAndOversizedFilesAreRejectedAndLeftIntact() throws {
    let url = try temporary().appendingPathComponent("annotations.json")
    let corrupt = Data("{malformed JSON".utf8)
    try corrupt.write(to: url)
    XCTAssertThrowsError(try AnnotationDocument.load(from: url))
    XCTAssertThrowsError(try AnnotationDocument.save([note()], to: url))
    XCTAssertEqual(try Data(contentsOf: url), corrupt)
    let oversized = Data(repeating: 32, count: 2 * 1024 * 1024 + 1)
    try oversized.write(to: url)
    XCTAssertThrowsError(try AnnotationDocument.readBounded(url))
    XCTAssertEqual(try Data(contentsOf: url).count, oversized.count)
  }
  func testFilesystemFailureDoesNotEraseExistingData() throws {
    let parent = try temporary().appendingPathComponent("occupied")
    let before = Data("existing file".utf8)
    try before.write(to: parent)
    XCTAssertThrowsError(
      try AnnotationDocument.save([note()], to: parent.appendingPathComponent("annotations.json")))
    XCTAssertEqual(try Data(contentsOf: parent), before)
  }
}
