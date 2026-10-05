import AppKit

let arguments = CommandLine.arguments
do {
    if arguments.count > 1 && arguments[1] == "--self-test" {
        try runInstallerTests()
    } else if arguments.count == 5 && arguments[1] == "--make-manifest" {
        let payload = URL(fileURLWithPath: arguments[2]).standardizedFileURL
        let version = try InstallerEngine.identity(at: payload)
        guard version == arguments[4] else { throw InstallError.message("Payload is \(version); expected \(arguments[4]).") }
        try InstallerEngine.run("/usr/bin/codesign", ["--verify", "--deep", "--strict", payload.path])
        let manifest = PayloadManifest(format: 1, version: version, bundleIdentifier: InstallerEngine.identifier,
            componentID: InstallerEngine.componentID, controllerID: InstallerEngine.controllerID,
            files: try InstallerEngine.fileHashes(in: payload))
        let encoder = JSONEncoder()
        encoder.outputFormatting = [.prettyPrinted, .sortedKeys]
        try encoder.encode(manifest).write(to: URL(fileURLWithPath: arguments[3]), options: .atomic)
        print("Manifest created: \(version), \(manifest.files.count) verified files.")
    } else if arguments.count == 2 && arguments[1] == "--inspect-payload" {
        let resources = Bundle.main.resourceURL!
        let manifest = try InstallerEngine.readManifest(resources.appendingPathComponent("payload-manifest.json"))
        let engine = InstallerEngine(payload: resources.appendingPathComponent("Payload/YOI-BOI.vst3"),
            manifest: manifest, destination: URL(fileURLWithPath: "/unused/YOI-BOI.vst3"),
            systemCopy: URL(fileURLWithPath: "/unused-system/YOI-BOI.vst3"), runningHosts: { [] })
        try engine.verifyPayload(at: engine.payload)
        print("Embedded YOI-BOI \(manifest.version): all file hashes, plug-in identities and code signature verified.")
    } else if arguments.count == 1 {
        let application = NSApplication.shared
        let delegate = InstallerDelegate()
        application.delegate = delegate
        application.setActivationPolicy(.regular)
        application.run()
        withExtendedLifetime(delegate) {}
    } else {
        throw InstallError.message("Unknown installer option.")
    }
} catch {
    fputs("YOI-BOI installer: \(error.localizedDescription)\n", stderr)
    exit(1)
}
