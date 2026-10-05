import AppKit
import ImageIO
import UniformTypeIdentifiers

// Export the three approved, already-rendered artwork cells without repainting.
// Platform resampling is used only for the required macOS icon representations.
let arguments = CommandLine.arguments
guard arguments.count == 3 else { fatalError("Usage: export-approved-icons.swift <approved-triptych.png> <output-directory>") }
let sourceURL = URL(fileURLWithPath: arguments[1])
let outputURL = URL(fileURLWithPath: arguments[2], isDirectory: true)
guard let source = CGImageSourceCreateWithURL(sourceURL as CFURL, nil),
      let sheet = CGImageSourceCreateImageAtIndex(source, 0, nil),
      sheet.width == sheet.height * 3 else { fatalError("Expected the approved square-cell 3:1 triptych") }
let fileManager = FileManager.default
let cellSize = sheet.height
let plugins = ["yoi-boi", "shifty", "back-scratch"]
// Approved triptych figures are slightly offset from equal thirds. These bounds
// retain each complete figure and its local transparent margin, excluding peers.
let approvedBounds = [
    CGRect(x: 61, y: 0, width: 670, height: 724),
    CGRect(x: 738, y: 0, width: 668, height: 724),
    CGRect(x: 1415, y: 0, width: 666, height: 724)
]
let representations: [(String, Int)] = [
    ("icon_16x16.png", 16), ("icon_16x16@2x.png", 32),
    ("icon_32x32.png", 32), ("icon_32x32@2x.png", 64),
    ("icon_128x128.png", 128), ("icon_128x128@2x.png", 256),
    ("icon_256x256.png", 256), ("icon_256x256@2x.png", 512),
    ("icon_512x512.png", 512), ("icon_512x512@2x.png", 1024)
]
func writePNG(_ image: CGImage, to url: URL) {
    guard let destination = CGImageDestinationCreateWithURL(url as CFURL, UTType.png.identifier as CFString, 1, nil) else {
        fatalError("Cannot create PNG: \(url.path)")
    }
    CGImageDestinationAddImage(destination, image, nil)
    guard CGImageDestinationFinalize(destination) else { fatalError("Cannot finalize PNG: \(url.path)") }
}
func representation(_ image: CGImage, size: Int) -> CGImage {
    guard let colorSpace = CGColorSpace(name: CGColorSpace.sRGB),
          let context = CGContext(data: nil, width: size, height: size, bitsPerComponent: 8,
              bytesPerRow: size * 4, space: colorSpace,
              bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue) else { fatalError("Cannot create icon representation") }
    context.interpolationQuality = .high
    let scale = Double(size) * 0.88 / Double(max(image.width, image.height))
    let width = Double(image.width) * scale
    let height = Double(image.height) * scale
    context.draw(image, in: CGRect(x: (Double(size) - width) / 2,
        y: (Double(size) - height) / 2, width: width, height: height))
    guard let result = context.makeImage() else { fatalError("Cannot render icon representation") }
    return result
}
for (index, plugin) in plugins.enumerated() {
    let directory = outputURL.appendingPathComponent(plugin, isDirectory: true)
    let iconset = directory.appendingPathComponent("AppIcon.iconset", isDirectory: true)
    try fileManager.createDirectory(at: iconset, withIntermediateDirectories: true)
    let rectangle = approvedBounds[index]
    guard let approvedCell = sheet.cropping(to: rectangle) else { fatalError("Cannot export approved cell") }
    writePNG(approvedCell, to: directory.appendingPathComponent("approved-source.png"))
    writePNG(representation(approvedCell, size: 1024), to: directory.appendingPathComponent("icon.png"))
    for (name, size) in representations {
        writePNG(representation(approvedCell, size: size), to: iconset.appendingPathComponent(name))
    }
    print("Exported \(plugin): unchanged approved \(cellSize)px cell, plus 16–1024px macOS icon representations")
}
