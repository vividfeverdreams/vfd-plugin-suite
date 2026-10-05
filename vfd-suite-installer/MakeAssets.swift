import AppKit

// Render the supplied vector as a monochrome mark; no shape or proportions change.
guard CommandLine.arguments.count == 3,
      let source = NSImage(contentsOf: URL(fileURLWithPath: CommandLine.arguments[1])) else {
    fatalError("Usage: make-assets <VfdLogo.svg> <output-directory>")
}
let output = URL(fileURLWithPath: CommandLine.arguments[2])
let fm = FileManager.default
try fm.createDirectory(at: output, withIntermediateDirectories: true)
func bitmap(_ size: Int, draw: (NSRect) -> Void) -> NSBitmapImageRep {
    let representation = NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: size, pixelsHigh: size,
        bitsPerSample: 8, samplesPerPixel: 4, hasAlpha: true, isPlanar: false, colorSpaceName: .deviceRGB,
        bytesPerRow: size * 4, bitsPerPixel: 32)!
    NSGraphicsContext.saveGraphicsState()
    NSGraphicsContext.current = NSGraphicsContext(bitmapImageRep: representation)
    draw(NSRect(x: 0, y: 0, width: size, height: size))
    NSGraphicsContext.restoreGraphicsState()
    return representation
}
func drawMark(in rect: NSRect) {
    let width = min(rect.width, rect.height * source.size.width / source.size.height)
    let height = width * source.size.height / source.size.width
    let fitted = NSRect(x: rect.midX - width / 2, y: rect.midY - height / 2, width: width, height: height)
    source.draw(in: fitted, from: .zero, operation: .sourceOver, fraction: 1)
    NSColor(calibratedWhite: 0.95, alpha: 1).setFill()
    rect.fill(using: .sourceIn)
}
let mark = bitmap(640) { drawMark(in: $0.insetBy(dx: 16, dy: 16)) }
try mark.representation(using: .png, properties: [:])!.write(to: output.appendingPathComponent("VfdLogo.png"))
let markImage = NSImage(size: NSSize(width: 640, height: 640))
markImage.addRepresentation(mark)
let iconset = output.appendingPathComponent("AppIcon.iconset")
try fm.createDirectory(at: iconset, withIntermediateDirectories: true)
for size in [16, 32, 128, 256, 512] {
    for scale in [1, 2] {
        let pixels = size * scale
        let image = bitmap(pixels) { rect in
            let plate = rect.insetBy(dx: rect.width * 0.09, dy: rect.height * 0.09)
            NSColor(calibratedRed: 0.10, green: 0.105, blue: 0.125, alpha: 1).setFill()
            NSBezierPath(roundedRect: plate, xRadius: rect.width * 0.18, yRadius: rect.height * 0.18).fill()
            markImage.draw(in: rect.insetBy(dx: rect.width * 0.23, dy: rect.height * 0.23),
                from: .zero, operation: .sourceOver, fraction: 1)
        }
        let suffix = scale == 2 ? "@2x" : ""
        try image.representation(using: .png, properties: [:])!
            .write(to: iconset.appendingPathComponent("icon_\(size)x\(size)\(suffix).png"))
    }
}
// Write the standard PNG-backed ICNS container directly; no external converter is required.
func lengthBytes(_ count: Int) -> Data {
    var value = UInt32(count).bigEndian
    return withUnsafeBytes(of: &value) { Data($0) }
}
let representations: [(String, String)] = [
    ("icp4", "icon_16x16.png"), ("ic11", "icon_16x16@2x.png"),
    ("icp5", "icon_32x32.png"), ("ic12", "icon_32x32@2x.png"),
    ("ic07", "icon_128x128.png"), ("ic13", "icon_128x128@2x.png"),
    ("ic08", "icon_256x256.png"), ("ic14", "icon_256x256@2x.png"),
    ("ic09", "icon_512x512.png"), ("ic10", "icon_512x512@2x.png")
]
var chunks = Data()
for (type, filename) in representations {
    let png = try Data(contentsOf: iconset.appendingPathComponent(filename))
    chunks.append(Data(type.utf8)); chunks.append(lengthBytes(png.count + 8)); chunks.append(png)
}
var icns = Data("icns".utf8); icns.append(lengthBytes(chunks.count + 8)); icns.append(chunks)
let iconURL = output.appendingPathComponent("AppIcon.icns")
try icns.write(to: iconURL)
guard let verifiedIcon = NSImage(contentsOf: iconURL), !verifiedIcon.representations.isEmpty else {
    fatalError("Generated icon could not be decoded by AppKit.")
}
print("Rendered supplied SVG into suite icon and UI mark.")
