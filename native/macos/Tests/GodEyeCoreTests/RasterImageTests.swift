import CoreGraphics
import Foundation
import MetalKit
import XCTest

@testable import GodEyeCore

final class RasterImageTests: XCTestCase {
  // Generated 2x2 indexed PNG: red/green, blue/red. No external provider needed.
  private let indexed = Data(
    base64Encoded:
      "iVBORw0KGgoAAAANSUhEUgAAAAIAAAACCAMAAABFaP0WAAAACVBMVEX/AAAA/wAAAP8tSs2KAAAADklEQVR4nGNgYGRgYgAAAA4ABMaIfPgAAAAASUVORK5CYII="
  )!
  func testIndexedPNGBecomesBoundedRGBAWithoutChangingSize() throws {
    let image = try RasterImage.decode(indexed)
    XCTAssertEqual(image.width, 2)
    XCTAssertEqual(image.height, 2)
    XCTAssertEqual(image.bitsPerPixel, 32)
    XCTAssertEqual(image.colorSpace?.model, .rgb)
    let bytes = try XCTUnwrap(image.dataProvider?.data) as Data
    XCTAssertEqual(Array(bytes), [255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 0, 0, 255])
  }
  func testNormalizedIndexedTileCanBeUploadedToMetal() async throws {
    guard let device = MTLCreateSystemDefaultDevice() else {
      throw XCTSkip("Metal device unavailable")
    }
    let image = try RasterImage.decode(indexed)
    let texture = try await MTKTextureLoader(device: device).newTexture(
      cgImage: image,
      options: [.SRGB: false, .generateMipmaps: false, .origin: MTKTextureLoader.Origin.topLeft])
    XCTAssertEqual(texture.width, 2)
    XCTAssertEqual(texture.height, 2)
  }
  func testOversizedImageMetadataAndMalformedBodiesAreRejected() throws {
    XCTAssertThrowsError(try RasterImage.decode(Data("not an image".utf8)))
    let large = Data(
      base64Encoded:
        "iVBORw0KGgoAAAANSUhEUgAABAEAAAABCAMAAACYEkc5AAAACVBMVEX/AAAA/wAAAP8tSs2KAAAAEUlEQVR4nGNgGAWjYBSMXAAABAIAARSFVjUAAAAASUVORK5CYII="
    )!
    XCTAssertThrowsError(try RasterImage.decode(large)) { error in
      guard case FeedError.oversized = error else { return XCTFail("Unexpected error: \(error)") }
    }
  }
}
