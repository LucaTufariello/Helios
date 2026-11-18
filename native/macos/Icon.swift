import AppKit

let directory = URL(fileURLWithPath: CommandLine.arguments[1], isDirectory: true)
try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)

func srgb(_ hex: Int) -> NSColor {
  NSColor(
    srgbRed: CGFloat((hex >> 16) & 0xff) / 255, green: CGFloat((hex >> 8) & 0xff) / 255,
    blue: CGFloat(hex & 0xff) / 255, alpha: 1)
}

let ink = srgb(0x10212c)
let sun = srgb(0xf7b955)
let horizon = srgb(0x6ed4dd)

for size in [16, 32, 128, 256, 512] {
  for scale in [1, 2] {
    let pixels = CGFloat(size * scale)
    let image = NSImage(size: NSSize(width: pixels, height: pixels))
    image.lockFocus()
    ink.setFill()
    NSBezierPath(
      roundedRect: NSRect(x: 0, y: 0, width: pixels, height: pixels).insetBy(
        dx: pixels * 0.045, dy: pixels * 0.045),
      xRadius: pixels * 0.20, yRadius: pixels * 0.20
    ).fill()

    // The horizon is a shallow arc, so it reads as the Earth's limb rather than
    // a rule. Curve ends sit at 0.3438 and the midpoint rises to 0.3652.
    func addHorizon(_ path: NSBezierPath, ends: CGFloat) {
      path.curve(
        to: NSPoint(x: pixels * 0.84, y: ends),
        controlPoint1: NSPoint(x: pixels * 0.3867, y: pixels * 0.3724),
        controlPoint2: NSPoint(x: pixels * 0.6133, y: pixels * 0.3724))
    }

    // Clip instead of masking: the disc emerges from the horizon, and nothing is
    // overpainted on top of the badge, which would leave a seam at small sizes.
    NSGraphicsContext.saveGraphicsState()
    let sky = NSBezierPath()
    sky.move(to: NSPoint(x: pixels * 0.16, y: pixels * 1.2))
    sky.line(to: NSPoint(x: pixels * 0.16, y: pixels * 0.3438))
    addHorizon(sky, ends: pixels * 0.3438)
    sky.line(to: NSPoint(x: pixels * 0.84, y: pixels * 1.2))
    sky.close()
    sky.addClip()

    // A fan of rays above the disc: Helios, with one line weight so the mark
    // stays legible down to 16 px.
    let center = NSPoint(x: pixels * 0.5, y: pixels * 0.5)
    let line = max(1, pixels * 0.035)
    sun.setStroke()
    for degrees in [45.0, 67.5, 90.0, 112.5, 135.0] {
      let angle = degrees * .pi / 180
      let ray = NSBezierPath()
      ray.move(
        to: NSPoint(
          x: center.x + cos(angle) * pixels * 0.235,
          y: center.y + sin(angle) * pixels * 0.235))
      ray.line(
        to: NSPoint(
          x: center.x + cos(angle) * pixels * 0.325,
          y: center.y + sin(angle) * pixels * 0.325))
      ray.lineWidth = line
      ray.lineCapStyle = .round
      ray.stroke()
    }
    sun.setFill()
    NSBezierPath(
      ovalIn: NSRect(
        x: center.x - pixels * 0.19, y: center.y - pixels * 0.19,
        width: pixels * 0.38, height: pixels * 0.38)
    ).fill()
    NSGraphicsContext.restoreGraphicsState()

    horizon.setStroke()
    let limb = NSBezierPath()
    limb.move(to: NSPoint(x: pixels * 0.16, y: pixels * 0.3438))
    addHorizon(limb, ends: pixels * 0.3438)
    limb.lineWidth = line
    limb.lineCapStyle = .round
    limb.stroke()

    image.unlockFocus()
    guard let tiff = image.tiffRepresentation, let bitmap = NSBitmapImageRep(data: tiff),
      let png = bitmap.representation(using: .png, properties: [:])
    else { fatalError("Cannot generate native icon") }
    let name = "icon_\(size)x\(size)" + (scale == 2 ? "@2x" : "") + ".png"
    try png.write(to: directory.appendingPathComponent(name))
  }
}
