import Foundation
import XCTest

@testable import GodEyeCore

final class SarTests: XCTestCase {
  func testProcessingStatesRejectInvalidTransitions() {
    XCTAssertTrue(SarState.queued.allows(.validating))
    XCTAssertTrue(SarState.validating.allows(.processing))
    XCTAssertTrue(SarState.processing.allows(.exporting))
    XCTAssertTrue(SarState.exporting.allows(.completed))
    XCTAssertTrue(SarState.processing.allows(.failed))
    XCTAssertTrue(SarState.failed.allows(.queued))
    XCTAssertFalse(SarState.completed.allows(.processing))
    XCTAssertFalse(SarState.processing.allows(.completed))
  }
  func testPersistedOutputAdmission() throws {
    let json = """
      {"id":"VV","preview":"VV-preview.png","visualization":"VV-display.tif", "scientific":"VV-sigma0.tif", "db":"VV-db.tif", "bounds":[8,45,9,46], "displayRanges":[[-25,0]], "width":100,"height":200}
      """
    var output = try JSONDecoder().decode(SarOutput.self, from: Data(json.utf8))
    XCTAssertTrue(output.hasValidBounds)
    let root = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
    XCTAssertEqual(
      output.file("VV-preview.png", in: root), root.appendingPathComponent("VV-preview.png"))
    for path in ["../escape", "/tmp/escape", "a/b", "a\\b", "..", ".", ""] {
      XCTAssertNil(output.file(path, in: root))
    }
    output.bounds = [170, 45, -170, 46]
    XCTAssertFalse(output.hasValidBounds)
    output.bounds = [8, 45, .infinity, 46]
    XCTAssertFalse(output.hasValidBounds)
  }
  func testInterruptedRecoveryPreservesLiveWorkerAndRemovesPartialOutputs() throws {
    let directory = FileManager.default.temporaryDirectory.appendingPathComponent(
      UUID().uuidString, isDirectory: true)
    try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
    defer { try? FileManager.default.removeItem(at: directory) }
    let json = """
      {"version":1,"workerPID":12345,"inputName":"test.SAFE","createdAt":"2026-10-04T12:00:00Z","state":"processing","outputs":[]}
      """
    let record = directory.appendingPathComponent("result.json")
    try Data(json.utf8).write(to: record)
    let partial = directory.appendingPathComponent("VV-sigma0.tif")
    try Data([1]).write(to: partial)
    let work = directory.appendingPathComponent(".sar-work-test", isDirectory: true)
    try FileManager.default.createDirectory(at: work, withIntermediateDirectories: true)
    try SarJobStorage.recoverInterrupted(in: directory, isWorkerRunning: { _ in true })
    XCTAssertTrue(FileManager.default.fileExists(atPath: partial.path))
    try SarJobStorage.recoverInterrupted(in: directory, isWorkerRunning: { _ in false })
    XCTAssertFalse(FileManager.default.fileExists(atPath: partial.path))
    XCTAssertFalse(FileManager.default.fileExists(atPath: work.path))
    let result = try SarProcessor.read(SarResult.self, from: record)
    XCTAssertEqual(result.state, .failed)
    XCTAssertTrue(result.error?.contains("interrupted") ?? false)
  }
  func testOptionsRetainScientificDefaults() throws {
    let options = try JSONDecoder().decode(
      SarOptions.self, from: JSONEncoder().encode(SarOptions()))
    XCTAssertEqual(options.polarizations, ["VV", "VH"])
    XCTAssertTrue(options.applyOrbit)
    XCTAssertFalse(options.speckle)
    XCTAssertEqual(options.pixelSpacing, 20)
    XCTAssertEqual(options.lowPercentile, 2)
    XCTAssertEqual(options.highPercentile, 98)
  }
  func testMissingRuntimeFailsClearly() async {
    let processor = SarProcessor(
      python: URL(fileURLWithPath: "/missing/sar-python"),
      worker: URL(fileURLWithPath: "/missing/worker"))
    do {
      _ = try await processor.inspect(
        input: URL(fileURLWithPath: "/missing/input"),
        destination: URL(fileURLWithPath: "/missing/output"), progress: { _ in })
      XCTFail("Missing runtime must not succeed")
    } catch { XCTAssertTrue(error.localizedDescription.contains("Configure")) }
  }
  func testProgressArrivesBeforeWorkerExits() async throws {
    let directory = FileManager.default.temporaryDirectory.appendingPathComponent(
      UUID().uuidString, isDirectory: true)
    try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
    defer { try? FileManager.default.removeItem(at: directory) }
    let worker = directory.appendingPathComponent("progress-fixture.py")
    let release = directory.appendingPathComponent("release")
    let destination = directory.appendingPathComponent("product.json")
    // The child requires a progress callback before it can finish. Its complete
    // stdout is much smaller than a pipe buffer, exposing read-until-full bugs.
    let script = """
      import json, pathlib, sys, time
      print(json.dumps({'state':'validating','stage':'Reading fixture','progress':0.02}), flush=True)
      print(json.dumps({'state':'processing','stage':'Waiting for acknowledgement','progress':None}), flush=True)
      deadline = time.monotonic() + 5
      while not pathlib.Path(__file__).with_name('release').exists():
          if time.monotonic() > deadline:
              sys.exit(2)
          time.sleep(0.01)
      product = {'mission':'Sentinel-1','satellite':'Generated test fixture','productType':'GRD',
                 'mode':'IW','acquisitionStart':'2026-01-01T00:00:00','acquisitionEnd':'2026-01-01T00:00:01',
                 'orbitDirection':'ASCENDING','polarizations':['VV'],'bands':[],'inputName':'fixture.SAFE'}
      pathlib.Path(sys.argv[sys.argv.index('--output')+1]).write_text(json.dumps(product))
      """
    try Data(script.utf8).write(to: worker)
    let processor = SarProcessor(python: URL(fileURLWithPath: "/usr/bin/python3"), worker: worker)
    let product = try await processor.inspect(
      input: directory.appendingPathComponent("fixture.SAFE"), destination: destination
    ) { update in
      if update.state == .processing { try? Data().write(to: release) }
    }
    XCTAssertEqual(product.satellite, "Generated test fixture")
    XCTAssertTrue(FileManager.default.fileExists(atPath: release.path))
  }
  func testBoundedResultReading() throws {
    let file = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
    defer { try? FileManager.default.removeItem(at: file) }
    try Data(repeating: 0, count: 1024 * 1024 + 1).write(to: file)
    XCTAssertThrowsError(try SarProcessor.read(SarResult.self, from: file))
  }
}
