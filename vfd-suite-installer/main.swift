import AppKit

let arguments = CommandLine.arguments
do {
    if arguments.count == 2 && arguments[1] == "--self-test" {
        try runInstallerTests()
    } else if arguments.count == 5 && arguments[1] == "--make-manifest" {
        let payloadRoot = URL(fileURLWithPath: arguments[2]).standardizedFileURL
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
        let manifest = SuiteManifest(format: 1, version: arguments[4], plugins: plugins)
        let engine = SuiteInstallerEngine(payloadRoot: payloadRoot, manifest: manifest, destinationRoot: URL(fileURLWithPath: "/unused"),
            systemRoot: URL(fileURLWithPath: "/unused-system"), runningHosts: { [] })
        try engine.inspectPayloads()
        let encoder = JSONEncoder(); encoder.outputFormatting = [.prettyPrinted, .sortedKeys]
        try encoder.encode(manifest).write(to: URL(fileURLWithPath: arguments[3]), options: .atomic)
        print("Suite \(manifest.version) manifest created; actual component/controller IDs and SHA-256 hashes captured for all five plug-ins.")
    } else if arguments.count == 2 && arguments[1] == "--inspect-payload" {
        guard let resources = Bundle.main.resourceURL else { throw InstallError.message("Run this command inside the built installer app.") }
        let manifest = try SuiteInstallerEngine.readManifest(resources.appendingPathComponent("suite-manifest.json"))
        let engine = SuiteInstallerEngine(payloadRoot: resources.appendingPathComponent("Payload"), manifest: manifest,
            destinationRoot: URL(fileURLWithPath: "/unused"), systemRoot: URL(fileURLWithPath: "/unused-system"), runningHosts: { [] })
        try engine.inspectPayloads()
        for entry in manifest.plugins {
            print("\(entry.name) \(entry.identity.version): SHA-256, bundle identity, VST3 identities, permissions and signature verified.")
            print("  component \(entry.identity.componentID); controller \(entry.identity.controllerID)")
        }
    } else if arguments.count == 3 && arguments[1] == "--render-preview" {
        _ = NSApplication.shared
        try InstallerDelegate().renderPreview(to: URL(fileURLWithPath: arguments[2]))
    } else if arguments.count == 2 && arguments[1] == "--check-hosts" {
        let hosts = InstallerDelegate.runningHosts(destinationRoot: InstallerDelegate.userVST3).sorted()
        print(hosts.isEmpty ? "No audio hosts are blocking the suite." : "Quit before installing: \(hosts.joined(separator: ", "))")
    } else if arguments.count == 1 {
        let application = NSApplication.shared
        let delegate = InstallerDelegate()
        application.delegate = delegate; application.setActivationPolicy(.regular); application.run()
        withExtendedLifetime(delegate) {}
    } else { throw InstallError.message("Options: --inspect-payload, --self-test, --check-hosts, --make-manifest <payload-dir> <manifest-file> <suite-version>.") }
} catch {
    fputs("Vivid Fever Dreams Suite installer: \(error.localizedDescription)\n", stderr)
    exit(1)
}
