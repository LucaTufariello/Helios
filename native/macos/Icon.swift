import AppKit

let directory = URL(fileURLWithPath: CommandLine.arguments[1], isDirectory: true)
try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
for size in [16, 32, 128, 256, 512] {
  for scale in [1, 2] {
    let pixels = size * scale
    let image = NSImage(size: NSSize(width: pixels, height: pixels))
    image.lockFocus()
    NSColor(calibratedRed: 0.09, green: 0.13, blue: 0.18, alpha: 1).setFill()
    NSBezierPath(
      roundedRect: NSRect(x: 0, y: 0, width: pixels, height: pixels).insetBy(
        dx: CGFloat(pixels) * 0.045, dy: CGFloat(pixels) * 0.045), xRadius: CGFloat(pixels) * 0.20,
      yRadius: CGFloat(pixels) * 0.20
    ).fill()
    NSColor(calibratedRed: 0.53, green: 0.74, blue: 0.91, alpha: 1).setStroke()
    let globe = NSRect(x: 0, y: 0, width: pixels, height: pixels).insetBy(
      dx: CGFloat(pixels) * 0.19, dy: CGFloat(pixels) * 0.19)
    let line = max(1, CGFloat(pixels) * 0.028)
    let circle = NSBezierPath(ovalIn: globe)
    circle.lineWidth = line
    circle.stroke()
    let meridian = NSBezierPath(ovalIn: globe.insetBy(dx: globe.width * 0.23, dy: 0))
    meridian.lineWidth = line
    meridian.stroke()
    let equator = NSBezierPath()
    equator.move(to: NSPoint(x: globe.minX, y: globe.midY))
    equator.line(to: NSPoint(x: globe.maxX, y: globe.midY))
    equator.lineWidth = line
    equator.stroke()
    image.unlockFocus()
    guard let tiff = image.tiffRepresentation, let bitmap = NSBitmapImageRep(data: tiff),
      let png = bitmap.representation(using: .png, properties: [:])
    else { fatalError("Cannot generate native icon") }
    let name = "icon_\(size)x\(size)" + (scale == 2 ? "@2x" : "") + ".png"
    try png.write(to: directory.appendingPathComponent(name))
  }
}
