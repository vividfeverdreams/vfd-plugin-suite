import AppKit

do {
    switch LaunchMode.parse(CommandLine.arguments) {
    case .selfTest:
        try runInstallerTests()
    case .makeManifest(let payloadPath, let manifestPath, let version):
        // realpath, not standardizedFileURL: the latter rewrites /private/tmp to the /tmp symlink, which is refused.
        let payloadRoot = InstallerEngine.canonical(URL(fileURLWithPath: payloadPath))
        let vst3 = payloadRoot.appendingPathComponent("BACK-SCRATCH.vst3")
        let audioUnit = payloadRoot.appendingPathComponent("BACK-SCRATCH.component")
        let identity = try InstallerEngine.identity(at: vst3, format: .vst3)
        let auIdentity = try InstallerEngine.identity(at: audioUnit, format: .au)
        guard identity.version == version, auIdentity.version == version else {
            throw InstallError.message("Payload versions are VST3 \(identity.version) and AU \(auIdentity.version); expected \(version).")
        }
        if let pinned = InstallerEngine.pinnedIdentity {
            guard identity.componentID == pinned.componentID, identity.controllerID == pinned.controllerID else {
                throw InstallError.message("The payload's VST3 identities differ from the released BACK-SCRATCH identities.")
            }
        }
        let units = auIdentity.audioUnits ?? []
        guard units.contains(InstallerEngine.audioUnit) else {
            throw InstallError.message("The AU registers \(units.map(\.description).joined(separator: ", ")); this installer expects \(InstallerEngine.audioUnit.description).")
        }
        try InstallerEngine.run("/usr/bin/codesign", ["--verify", "--deep", "--strict", vst3.path])
        try InstallerEngine.run("/usr/bin/codesign", ["--verify", "--deep", "--strict", audioUnit.path])
        let manifest = PayloadManifest(format: 2, version: identity.version, bundleIdentifier: InstallerEngine.identifier,
            componentID: identity.componentID ?? "", controllerID: identity.controllerID ?? "", audioUnits: units,
            vst3Files: try InstallerEngine.fileHashes(in: vst3), audioUnitFiles: try InstallerEngine.fileHashes(in: audioUnit))
        let encoder = JSONEncoder()
        encoder.outputFormatting = [.prettyPrinted, .sortedKeys]
        try encoder.encode(manifest).write(to: URL(fileURLWithPath: manifestPath), options: .atomic)
        print("Manifest created: \(identity.version), \(manifest.vst3Files.count) VST3 and \(manifest.audioUnitFiles.count) AU files verified.")
    case .checkWhatsNew(let path):
        let notes = try WhatsNew.parse(Data(contentsOf: URL(fileURLWithPath: path)), requiredNames: ["BACK-SCRATCH"], allowOtherNames: true)
        print("What’s new is valid: \(notes["BACK-SCRATCH"]?.count ?? 0) lines for BACK-SCRATCH.")
    case .inspectPayload:
        guard let resources = Bundle.main.resourceURL else { throw InstallError.message("Run this command inside the built installer app.") }
        let manifest = try InstallerEngine.readManifest(resources.appendingPathComponent("payload-manifest.json"))
        let engine = InstallerEngine(payloadRoot: resources.appendingPathComponent("Payload"), manifest: manifest,
            locations: .inside(URL(fileURLWithPath: "/unused")), runningHosts: { _ in [] })
        for format in PluginFormat.allCases.sorted() { try engine.verifyPayload(at: engine.payloadURL(format), format: format) }
        _ = try WhatsNew.parse(Data(contentsOf: resources.appendingPathComponent("whats-new.json")), requiredNames: ["BACK-SCRATCH"], allowOtherNames: true)
        print("Embedded BACK-SCRATCH \(manifest.version): VST3 and AU file hashes, plug-in identities, AU codes \(InstallerEngine.audioUnit.description), code signatures and What’s new verified.")
    case .checkHosts:
        let hosts = InstallerDelegate.runningHosts(formats: Set(PluginFormat.allCases)).sorted()
        print(hosts.isEmpty ? "No audio hosts are blocking the BACK-SCRATCH update."
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
    fputs("BACK-SCRATCH installer: \(error.localizedDescription)\n", stderr)
    exit(1)
}
