import AppKit

do {
    switch LaunchMode.parse(CommandLine.arguments) {
    case .selfTest:
        try runInstallerTests()
    case .makeManifest(let payloadPath, let manifestPath, let version):
        // realpath, not standardizedFileURL: the latter rewrites /private/tmp to the /tmp symlink, which is refused.
        let payload = InstallerEngine.canonical(URL(fileURLWithPath: payloadPath))
        let identity = try InstallerEngine.identity(at: payload)
        guard identity.version == version else { throw InstallError.message("Payload is \(identity.version); expected \(version).") }
        if let pinned = InstallerEngine.pinnedIdentity {
            guard identity.componentID == pinned.componentID, identity.controllerID == pinned.controllerID else {
                throw InstallError.message("The payload's VST3 identities differ from the released THROW-UP identities.")
            }
        }
        try InstallerEngine.run("/usr/bin/codesign", ["--verify", "--deep", "--strict", payload.path])
        let manifest = PayloadManifest(format: 1, version: identity.version, bundleIdentifier: InstallerEngine.identifier,
            componentID: identity.componentID, controllerID: identity.controllerID,
            files: try InstallerEngine.fileHashes(in: payload))
        let encoder = JSONEncoder()
        encoder.outputFormatting = [.prettyPrinted, .sortedKeys]
        try encoder.encode(manifest).write(to: URL(fileURLWithPath: manifestPath), options: .atomic)
        print("Manifest created: \(identity.version), \(manifest.files.count) verified files.")
    case .inspectPayload:
        guard let resources = Bundle.main.resourceURL else { throw InstallError.message("Run this command inside the built installer app.") }
        let manifest = try InstallerEngine.readManifest(resources.appendingPathComponent("payload-manifest.json"))
        let engine = InstallerEngine(payload: resources.appendingPathComponent("Payload/THROW-UP.vst3"),
            manifest: manifest, destination: URL(fileURLWithPath: "/unused/THROW-UP.vst3"),
            systemCopy: URL(fileURLWithPath: "/unused-system/THROW-UP.vst3"),
            recoveryRoot: URL(fileURLWithPath: "/unused-recovered"), runningHosts: { [] })
        try engine.verifyPayload(at: engine.payload)
        print("Embedded THROW-UP \(manifest.version): all file hashes, plug-in identities and code signature verified.")
    case .checkHosts:
        let destination = FileManager.default.homeDirectoryForCurrentUser
            .appendingPathComponent("Library/Audio/Plug-Ins/VST3/THROW-UP.vst3")
        let hosts = InstallerDelegate.runningHosts(destination: destination).sorted()
        print(hosts.isEmpty ? "No audio hosts are blocking the THROW-UP update."
                           : "Audio hosts still running: \(hosts.joined(separator: ", "))")
    case .gui:
        let application = NSApplication.shared
        let delegate = InstallerDelegate()
        application.delegate = delegate
        application.setActivationPolicy(.regular)
        application.run()
        withExtendedLifetime(delegate) {}
    case .invalid:
        throw InstallError.message("Unknown installer option.")
    }
} catch {
    fputs("THROW-UP installer: \(error.localizedDescription)\n", stderr)
    exit(1)
}
