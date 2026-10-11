import AppKit
import CoreML
import CryptoKit
import GodEyeCore
import ImageIO
import SwiftUI
import Vision

/// Local Vision-compatible object detectors only; no classifier labels are converted into boxes.
@MainActor final class MissionDetection: ObservableObject {
  @Published private(set) var modelURL: URL?
  @Published private(set) var busy = false
  @Published private(set) var proposals: [MissionObservation] = []
  @Published private(set) var missionID: UUID?
  @Published var error: String?
  @Published var minimumScore = 0.5
  private var task: Task<Void, Never>?
  func chooseModel() {
    let panel = NSOpenPanel()
    panel.message =
      "Choose a Core ML object detector compatible with Vision (recognized objects with bounding boxes)."
    panel.canChooseDirectories = true
    guard panel.runModal() == .OK, let url = panel.url,
      ["mlmodel", "mlpackage", "mlmodelc"].contains(url.pathExtension.lowercased())
    else { return }
    modelURL = url
    proposals = []
    error = nil
  }
  func cancel() {
    task?.cancel()
    task = nil
    busy = false
    proposals = []
    missionID = nil
  }
  func run(image: SavedOpticalImage, evidenceID: UUID, missionID: UUID) {
    guard !busy, let modelURL, let bounds = image.result.bounds,
      ChangeReviewResult.validBounds(bounds)
    else { return }
    self.missionID = missionID
    proposals = []
    busy = true
    error = nil
    let preview = image.directory.appendingPathComponent("preview.png")
    let acquired = image.result.scene?.acquiredAt.flatMapDate
    let threshold = minimumScore
    task = Task {
      do {
        let work = Task.detached(priority: .userInitiated) {
          try Self.detect(
            modelURL: modelURL, preview: preview, bounds: bounds, evidenceID: evidenceID,
            observedAt: acquired, threshold: threshold)
        }
        let values = try await withTaskCancellationHandler(
          operation: { try await work.value }, onCancel: { work.cancel() })
        try Task.checkCancellation()
        proposals = values
        busy = false
      } catch {
        guard !Task.isCancelled else { return }
        self.error =
          "The model or image could not be processed. Use a Vision-compatible object detector; classifiers and raw tensor models are unsupported."
        busy = false
      }
    }
  }
  private nonisolated static func detect(
    modelURL: URL, preview: URL, bounds: [Double], evidenceID: UUID,
    observedAt: Date?, threshold: Double
  ) throws -> [MissionObservation] {
    guard let size = try preview.resourceValues(forKeys: [.fileSizeKey]).fileSize,
      size > 0, size <= 32 * 1024 * 1024
    else { throw FeedError.malformed }
    let data = try Data(contentsOf: preview, options: .mappedIfSafe)
    guard data.count <= 32 * 1024 * 1024,
      let source = CGImageSourceCreateWithData(data as CFData, nil),
      let props = CGImageSourceCopyPropertiesAtIndex(source, 0, nil) as? [String: Any],
      let w = props[kCGImagePropertyPixelWidth as String] as? Int,
      let h = props[kCGImagePropertyPixelHeight as String] as? Int,
      w > 0, h > 0, w <= 2048, h <= 2048,
      let image = CGImageSourceCreateImageAtIndex(source, 0, nil)
    else { throw FeedError.malformed }
    try Task.checkCancellation()
    let compiled =
      modelURL.pathExtension == "mlmodelc" ? modelURL : try MLModel.compileModel(at: modelURL)
    defer { if compiled != modelURL { try? FileManager.default.removeItem(at: compiled) } }
    let configuration = MLModelConfiguration()
    configuration.computeUnits = .all
    let model = try MLModel(contentsOf: compiled, configuration: configuration)
    let request = VNCoreMLRequest(model: try VNCoreMLModel(for: model))
    // Full-image scaleFill keeps normalized output boxes mapped to the whole preview.
    request.imageCropAndScaleOption = .scaleFill
    try VNImageRequestHandler(cgImage: image, orientation: .up).perform([request])
    try Task.checkCancellation()
    guard let results = request.results as? [VNRecognizedObjectObservation] else {
      throw FeedError.malformed
    }
    let fingerprint = SHA256.hash(data: data).map { String(format: "%02x", $0) }.joined()
    return results.sorted { $0.confidence > $1.confidence }.prefix(200).compactMap { result in
      guard let label = result.labels.first, Double(label.confidence) >= threshold else {
        return nil
      }
      let r = result.boundingBox
      guard
        let b = MissionImageGeometry.bounds(
          x: r.minX, y: r.minY, width: r.width, height: r.height, image: bounds),
        let center = Coordinate(latitude: (b[1] + b[3]) / 2, longitude: (b[0] + b[2]) / 2)
      else { return nil }
      var item = MissionObservation(
        title: String(label.identifier.prefix(240)), category: String(label.identifier.prefix(120)),
        coordinate: center,
        method: "Core ML proposal · \(modelURL.lastPathComponent.prefix(200)) · Vision scaleFill")
      item.modelScore = Double(label.confidence)
      item.modelName = String(modelURL.lastPathComponent.prefix(200))
      item.inputSHA256 = fingerprint
      item.inputTransform = "Vision scaleFill; normalized lower-left boxes on EPSG:4326 preview"
      item.bounds = b
      item.evidenceID = evidenceID
      item.observedAt = observedAt
      item.note =
        "Unreviewed model output. Preview SHA256: \(fingerprint). Model score is not calibrated probability. Geographic accuracy follows the source preview."
      return item
    }
  }
}
extension String {
  fileprivate var flatMapDate: Date? { ProviderParsers.date(self) }
}
