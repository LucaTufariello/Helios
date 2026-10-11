import Darwin
import Foundation

public enum SarState: String, Codable, Sendable {
  case queued, validating, processing, exporting, completed, failed
  public var isActive: Bool { [.queued, .validating, .processing, .exporting].contains(self) }
  public func allows(_ next: SarState) -> Bool {
    if next == self { return true }
    switch self {
    case .queued: return [.validating, .failed].contains(next)
    case .validating: return [.processing, .completed, .failed].contains(next)
    case .processing: return [.exporting, .failed].contains(next)
    case .exporting: return [.completed, .failed].contains(next)
    case .completed, .failed: return next == .queued
    }
  }
}
public struct SarProgress: Codable, Sendable {
  public var state: SarState
  public var stage: String
  public var progress: Double?
}
public struct SarBand: Codable, Sendable {
  public var polarization: String
  public var width: Int
  public var height: Int
  public var sourcePixelSpacing: Double
}
public struct SarProduct: Codable, Sendable {
  public var mission: String
  public var satellite: String
  public var productType: String
  public var mode: String
  public var acquisitionStart: String
  public var acquisitionEnd: String
  public var orbitDirection: String
  public var relativeOrbit: Int?
  public var polarizations: [String]
  public var bands: [SarBand]
  public var inputName: String
}
public struct SarOptions: Codable, Sendable {
  public var polarizations = ["VV", "VH"]
  public var applyOrbit = true
  public var speckle = false
  public var pixelSpacing = 20.0
  public var lowPercentile = 2.0
  public var highPercentile = 98.0
  /// Source-dependent admission records whether noise removal was needed;
  /// this is provenance, not a new user-controlled processing toggle.
  public var removeThermalNoise: Bool?
  public init() {}
}
public struct SarOutput: Codable, Identifiable, Sendable {
  public var id: String
  public var preview: String
  public var visualization: String
  public var scientific: String?
  public var db: String?
  /// West, south, east, north; EPSG:4326 pixel outer edges, not center points.
  public var bounds: [Double]
  public var displayRanges: [[Double]]
  public var width: Int
  public var height: Int
  public var hasValidDisplay: Bool {
    hasValidBounds && width > 0 && height > 0 && displayRanges.count == (id == "Composite" ? 3 : 1)
      && displayRanges.allSatisfy { $0.count == 2 && $0.allSatisfy(\.isFinite) && $0[0] < $0[1] }
  }
  public var hasValidBounds: Bool {
    bounds.count == 4 && bounds.allSatisfy(\.isFinite)
      && bounds[0] >= -180 && bounds[2] <= 180 && bounds[0] < bounds[2]
      && bounds[1] >= -85 && bounds[3] <= 85 && bounds[1] < bounds[3]
      && bounds[2] - bounds[0] <= 180
  }
  public func file(_ name: String, in directory: URL) -> URL? {
    guard !name.isEmpty, name == URL(fileURLWithPath: name).lastPathComponent,
      !name.contains("/"), !name.contains("\\"), !name.contains(":"), name != ".", name != ".."
    else { return nil }
    let file = directory.appendingPathComponent(name)
    guard !file.hasDirectoryPath,
      file.resolvingSymlinksInPath().deletingLastPathComponent().path
        == directory.resolvingSymlinksInPath().path
    else { return nil }
    return file
  }
}
public struct SarResult: Codable, Sendable {
  public var version: Int
  public var workerPID: Int32?
  public var inputName: String
  public var createdAt: String
  public var state: SarState
  public var metadata: SarProduct?
  public var options: SarOptions?
  public var outputs: [SarOutput]
  public var error: String?
  public var completedAt: String?
  public var elapsedSeconds: Double?
}
public struct SarOverlay: Equatable, Sendable {
  public var preview: URL
  public var bounds: [Double]
  public var label: String
  public var pixelGrid: Bool
  public var opacity: Float
  public init(
    preview: URL, bounds: [Double], label: String, pixelGrid: Bool = false, opacity: Float = 0.9
  ) {
    self.preview = preview
    self.bounds = bounds
    self.label = label
    self.pixelGrid = pixelGrid
    self.opacity = opacity
  }
}
public enum SarError: Error, LocalizedError {
  case setup
  case failed(String)
  case oversized
  public var errorDescription: String? {
    switch self {
    case .setup:
      "Configure the SAR Python interpreter in Satellite → SAR workflow → Processing engine setup. SNAP GPT is also required when processing raw GRD products."
    case .failed(let message): message
    case .oversized: "The SAR worker returned an oversized response."
    }
  }
}

/// Local process boundary. External command syntax stays out of workspace/UI code.
/// Blocking pipe reads run in a detached task; cancellation terminates the worker,
/// whose SIGTERM handler terminates the GPT process group and cleans intermediates.
public struct SarProcessor: Sendable {
  public let python: URL
  public let worker: URL
  public init(python: URL, worker: URL) {
    self.python = python
    self.worker = worker
  }
  public func inspect(
    input: URL, destination: URL,
    progress: @escaping @Sendable (SarProgress) -> Void
  ) async throws -> SarProduct {
    try await invoke(
      ["inspect", "--input", input.path, "--output", destination.path], progress: progress)
    return try Self.read(SarProduct.self, from: destination)
  }
  public func process(
    input: URL, destination: URL, gpt: URL, options: SarOptions,
    progress: @escaping @Sendable (SarProgress) -> Void
  ) async throws -> SarResult {
    let options = String(decoding: try JSONEncoder().encode(options), as: UTF8.self)
    try await invoke(
      [
        "process", "--input", input.path, "--output", destination.path,
        "--gpt", gpt.path, "--options", options,
      ], progress: progress)
    return try Self.read(SarResult.self, from: destination.appendingPathComponent("result.json"))
  }
  public static func read<T: Decodable>(
    _ type: T.Type, from url: URL, maximumBytes: Int = 1024 * 1024
  ) throws -> T {
    guard (1...8 * 1024 * 1024).contains(maximumBytes) else { throw SarError.oversized }
    let handle = try FileHandle(forReadingFrom: url)
    defer { try? handle.close() }
    let data = try handle.read(upToCount: maximumBytes + 1) ?? Data()
    guard data.count <= maximumBytes else { throw SarError.oversized }
    let decoder = JSONDecoder()
    // New portable review documents use ISO UTC dates. Accept the original
    // native JSON reference-date numbers when loading previous local documents.
    decoder.dateDecodingStrategy = .custom { decoder in
      let value = try decoder.singleValueContainer()
      if let seconds = try? value.decode(Double.self), seconds.isFinite {
        return Date(timeIntervalSinceReferenceDate: seconds)
      }
      let text = try value.decode(String.self)
      guard let date = ProviderParsers.date(text) else {
        throw DecodingError.dataCorruptedError(in: value, debugDescription: "Invalid UTC date")
      }
      return date
    }
    return try decoder.decode(type, from: data)
  }
  public func compare(
    request: URL, destination: URL,
    progress: @escaping @Sendable (SarProgress) -> Void
  ) async throws -> ChangeReviewResult {
    try await invoke(
      ["compare", "--input", request.path, "--output", destination.path], progress: progress)
    let result = try Self.read(
      ChangeReviewResult.self, from: destination.appendingPathComponent("result.json"))
    guard result.isValid, result.state == .completed else {
      throw SarError.failed("The saved change-review result is invalid.")
    }
    return result
  }
  /// Reuse the bounded, isolated process adapter for the optical raster worker.
  public func renderOptical(
    request: URL, destination: URL,
    progress: @escaping @Sendable (SarProgress) -> Void
  ) async throws -> SentinelOpticalResult {
    guard FileManager.default.isExecutableFile(atPath: python.path) else {
      throw SarError.failed(
        "Configure the Python raster environment in Satellite → SAR workflow → Processing engine setup. SNAP is not required for optical viewing."
      )
    }
    try await invoke(
      ["render", "--input", request.path, "--output", destination.path], progress: progress)
    let result = try Self.read(
      SentinelOpticalResult.self, from: destination.appendingPathComponent("result.json"))
    guard result.isValid, result.state == .completed else {
      throw SarError.failed("The saved optical image result is invalid.")
    }
    return result
  }
  private func invoke(_ arguments: [String], progress: @escaping @Sendable (SarProgress) -> Void)
    async throws
  {
    guard FileManager.default.isExecutableFile(atPath: python.path), worker.isFileURL else {
      throw SarError.setup
    }
    let control = SarProcessControl()
    try await withTaskCancellationHandler {
      try await Task.detached(priority: .userInitiated) {
        let process = Process()
        process.executableURL = python
        process.arguments = ["-I", worker.path] + arguments
        let pipe = Pipe()
        process.standardOutput = pipe
        process.standardError = FileHandle.nullDevice
        var environment = ProcessInfo.processInfo.environment
        // Python isolated mode ignores PYTHONPATH and user-site imports.
        environment["PYTHONUNBUFFERED"] = "1"
        process.environment = environment
        try control.start(process)
        var pending = Data()
        var buffer = [UInt8](repeating: 0, count: 8192)
        var failure = "SAR processing failed. Inspect the local diagnostic log."
        do {
          while true {
            // FileHandle.read(upToCount:) can wait to fill the requested size.
            // A pipe read returns currently available bytes, delivering flushed
            // status lines while a quiet, long-running SNAP job is still alive.
            let count = buffer.withUnsafeMutableBytes {
              Darwin.read(pipe.fileHandleForReading.fileDescriptor, $0.baseAddress, $0.count)
            }
            if count == 0 { break }
            if count < 0 {
              if errno == EINTR { continue }
              throw POSIXError(POSIXErrorCode(rawValue: errno) ?? .EIO)
            }
            pending.append(contentsOf: buffer.prefix(count))
            guard pending.count <= 128 * 1024 else { throw SarError.oversized }
            while let newline = pending.firstIndex(of: 10) {
              let line = pending.prefix(upTo: newline)
              pending.removeSubrange(...newline)
              if let update = try? JSONDecoder().decode(SarProgress.self, from: line) {
                if update.state == .failed { failure = String(update.stage.prefix(600)) }
                progress(update)
              }
            }
          }
          process.waitUntilExit()
          if control.cancelled { throw CancellationError() }
          guard process.terminationStatus == 0 else { throw SarError.failed(failure) }
        } catch {
          control.cancel()
          process.waitUntilExit()
          throw error
        }
      }.value
    } onCancel: {
      control.cancel()
    }
  }
}
private final class SarProcessControl: @unchecked Sendable {
  private let lock = NSLock()
  private var process: Process?
  private var wasCancelled = false
  var cancelled: Bool { lock.withLock { wasCancelled } }
  func start(_ process: Process) throws {
    try lock.withLock {
      if wasCancelled { throw CancellationError() }
      self.process = process
      try process.run()
    }
  }
  func cancel() {
    lock.withLock {
      wasCancelled = true
      if let process, process.isRunning { process.terminate() }
    }
  }
}

/// Recover a crashed job only when its recorded worker process is confirmed gone.
/// A second app instance must never clean a still-running worker's directory.
public enum SarJobStorage {
  public static func recoverInterrupted(
    in directory: URL,
    isWorkerRunning: (Int32) -> Bool
  ) throws {
    let record = directory.appendingPathComponent("result.json")
    let job = try SarProcessor.read(SarResult.self, from: record)
    guard job.state.isActive, let pid = job.workerPID, pid > 0, !isWorkerRunning(pid) else {
      return
    }
    let files = try FileManager.default.contentsOfDirectory(
      at: directory,
      includingPropertiesForKeys: nil)
    for file in files
    where file.lastPathComponent.hasPrefix(".sar-work-")
      || ["tif", "png", "tmp"].contains(file.pathExtension)
    {
      try FileManager.default.removeItem(at: file)
    }
    guard
      var metadata = try JSONSerialization.jsonObject(with: Data(contentsOf: record))
        as? [String: Any]
    else { throw SarError.failed("Invalid saved SAR job.") }
    metadata["state"] = "failed"
    metadata["outputs"] = []
    metadata["error"] =
      "Processing was interrupted. Temporary and partial outputs were removed; import the original product to retry."
    metadata["updatedAt"] = ISO8601DateFormatter().string(from: Date())
    try JSONSerialization.data(withJSONObject: metadata, options: [.sortedKeys, .prettyPrinted])
      .write(to: record, options: [.atomic])
  }
}
