import AppKit

let arguments = CommandLine.arguments
do {
    if arguments.count > 1 && arguments[1] == "--self-test" {
        try runInstallerTests()
    } else if arguments.count == 5 && arguments[1] == "--make-manifest" {
        let payload = URL(fileURLWithPath: arguments[2])
        let identity = try InstallerEngine.identity(at: payload)
        guard identity.version == arguments[4] else { throw InstallError.message("Payload is \(identity.version); expected \(arguments[4]).") }
        try InstallerEngine.run("/usr/bin/codesign", ["--verify", "--deep", "--strict", payload.path])
        let manifest = PayloadManifest(format: 1, version: identity.version, bundleIdentifier: InstallerEngine.identifier,
            componentID: identity.componentID, controllerID: identity.controllerID,
            files: try InstallerEngine.fileHashes(in: payload))
        let encoder = JSONEncoder()
        encoder.outputFormatting = [.prettyPrinted, .sortedKeys]
        try encoder.encode(manifest).write(to: URL(fileURLWithPath: arguments[3]), options: .atomic)
        print("Manifest created: \(identity.version), \(manifest.files.count) verified files.")
    } else if arguments.count == 2 && arguments[1] == "--inspect-payload" {
        let resources = Bundle.main.resourceURL!
        let manifest = try InstallerEngine.readManifest(resources.appendingPathComponent("payload-manifest.json"))
        let engine = InstallerEngine(payload: resources.appendingPathComponent("Payload/THROW-UP.vst3"),
            manifest: manifest, destination: URL(fileURLWithPath: "/unused/THROW-UP.vst3"),
            systemCopy: URL(fileURLWithPath: "/unused-system/THROW-UP.vst3"), runningHosts: { [] })
        try engine.verifyPayload(at: engine.payload)
        print("Embedded THROW-UP \(manifest.version): all file hashes, plug-in identities and code signature verified.")
    } else if arguments.count == 2 && arguments[1] == "--check-hosts" {
        let destination = FileManager.default.homeDirectoryForCurrentUser
            .appendingPathComponent("Library/Audio/Plug-Ins/VST3/THROW-UP.vst3")
        let hosts = InstallerDelegate.runningHosts(destination: destination).sorted()
        print(hosts.isEmpty ? "No audio hosts are blocking the THROW-UP update."
                           : "Audio hosts still running: \(hosts.joined(separator: ", "))")
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
    fputs("THROW-UP installer: \(error.localizedDescription)\n", stderr)
    exit(1)
}
