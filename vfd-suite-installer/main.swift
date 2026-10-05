import AppKit

do {
    switch LaunchMode.parse(CommandLine.arguments) {
    case .selfTest:
        try runInstallerTests()
    case .makeManifest(let payloadPath, let manifestPath, let suiteVersion):
        let payloadRoot = URL(fileURLWithPath: payloadPath).standardizedFileURL
        var plugins: [PayloadManifest] = []
        for definition in SuiteCatalog.plugins {
            let payload = payloadRoot.appendingPathComponent(definition.filename)
            let identity = try SuiteInstallerEngine.identity(at: payload, definition: definition)
            guard identity.version == definition.version else {
                throw InstallError.message("\(definition.name) is \(identity.version); expected \(definition.version).")
            }
            try SuiteInstallerEngine.run("/usr/bin/codesign", ["--verify", "--deep", "--strict", payload.path])
            plugins.append(PayloadManifest(name: definition.name, filename: definition.filename, bundleIdentifier: definition.bundleIdentifier,
                identity: identity, files: try SuiteInstallerEngine.fileHashes(in: payload)))
        }
        let manifest = SuiteManifest(format: 1, version: suiteVersion, plugins: plugins)
        let engine = SuiteInstallerEngine(payloadRoot: payloadRoot, manifest: manifest, destinationRoot: URL(fileURLWithPath: "/unused"),
            systemRoot: URL(fileURLWithPath: "/unused-system"), runningHosts: { [] })
        try engine.inspectPayloads()
        let encoder = JSONEncoder(); encoder.outputFormatting = [.prettyPrinted, .sortedKeys]
        try encoder.encode(manifest).write(to: URL(fileURLWithPath: manifestPath), options: .atomic)
        print("Suite \(manifest.version) manifest created; actual component/controller IDs and SHA-256 hashes captured for all five plug-ins.")
    case .inspectPayload:
        guard let resources = Bundle.main.resourceURL else { throw InstallError.message("Run this command inside the built installer app.") }
        let manifest = try SuiteInstallerEngine.readManifest(resources.appendingPathComponent("suite-manifest.json"))
        let engine = SuiteInstallerEngine(payloadRoot: resources.appendingPathComponent("Payload"), manifest: manifest,
            destinationRoot: URL(fileURLWithPath: "/unused"), systemRoot: URL(fileURLWithPath: "/unused-system"), runningHosts: { [] })
        try engine.inspectPayloads()
        for entry in manifest.plugins {
            print("\(entry.name) \(entry.identity.version): SHA-256, bundle identity, VST3 identities, permissions and signature verified.")
            print("  component \(entry.identity.componentID); controller \(entry.identity.controllerID)")
        }
    case .renderPreview(let path):
        _ = NSApplication.shared
        try InstallerDelegate().renderPreview(to: URL(fileURLWithPath: path))
    case .checkHosts:
        let hosts = InstallerDelegate.runningHosts(destinationRoot: InstallerDelegate.userVST3).sorted()
        print(hosts.isEmpty ? "No audio hosts are blocking the suite." : "Quit before installing: \(hosts.joined(separator: ", "))")
        exit(hosts.isEmpty ? 0 : 2)
    case .gui:
        let application = NSApplication.shared
        let delegate = InstallerDelegate()
        application.delegate = delegate; application.setActivationPolicy(.regular); application.run()
        withExtendedLifetime(delegate) {}
    case .invalid:
        throw InstallError.message("Options: --inspect-payload, --self-test, --check-hosts, --render-preview <png>, --make-manifest <payload-dir> <manifest-file> <suite-version>.")
    }
} catch {
    fputs("Vivid Fever Dreams Suite installer: \(UserFacingText.logText(for: error))\n", stderr)
    exit(1)
}
