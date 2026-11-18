import CoreGraphics
import Foundation
import ImageIO

/// Normalize indexed/gray PNG and JPEG provider tiles to a bounded native RGBA image.
public enum RasterImage {
  public static func decode(_ data: Data) throws -> CGImage {
    guard data.count <= 2 * 1024 * 1024 else { throw FeedError.oversized }
    guard let source = CGImageSourceCreateWithData(data as CFData, nil),
      CGImageSourceGetCount(source) == 1,
      let properties = CGImageSourceCopyPropertiesAtIndex(source, 0, nil) as? [String: Any],
      let width = properties[kCGImagePropertyPixelWidth as String] as? Int,
      let height = properties[kCGImagePropertyPixelHeight as String] as? Int,
      width > 0, height > 0
    else { throw FeedError.malformed }
    guard width <= 1024, height <= 1024 else { throw FeedError.oversized }
    guard
      let image = CGImageSourceCreateImageAtIndex(
        source, 0, [kCGImageSourceShouldCacheImmediately: true] as CFDictionary),
      let colorSpace = CGColorSpace(name: CGColorSpace.sRGB),
      let context = CGContext(
        data: nil, width: width, height: height, bitsPerComponent: 8, bytesPerRow: width * 4,
        space: colorSpace, bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)
    else { throw FeedError.malformed }
    context.setBlendMode(.copy)
    context.interpolationQuality = .none
    context.draw(image, in: CGRect(x: 0, y: 0, width: width, height: height))
    guard let rgba = context.makeImage() else { throw FeedError.malformed }
    return rgba
  }
}
