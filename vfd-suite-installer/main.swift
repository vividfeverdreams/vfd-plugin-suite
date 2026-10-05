import AppKit

do {
    switch LaunchMode.parse(CommandLine.arguments) {
    case .selfTest:
        try runInstallerTests()
    case .makeManifest(let payloadPath, let manifestPath, let suiteVersion):
        let payloadRoot = SuiteInstallerEngine.canonical(URL(fileURLWithPath: payloadPath))
        var entries: [PayloadEntry] = []
        for key in SuiteCatalog.allItems {
            let definition = key.definition
            let payload = payloadRoot.appendingPathComponent(key.filename)
            let identity = try SuiteInstallerEngine.identity(at: payload, definition: definition, format: key.format)
            guard identity.version == definition.version else {
                throw InstallError.message("\(key.title) is \(identity.version); expected \(definition.version).")
            }
            if key.format == .au {
                let found = (identity.audioUnits ?? []).map(\.description).joined(separator: ", ")
                guard identity.audioUnits?.contains(definition.audioUnit) == true else {
                    throw InstallError.message("\(key.title) registers \(found); the suite expects \(definition.audioUnit.description). Update SuiteCatalog if the plug-in's AU codes changed on purpose.")
                }
            }
            try SuiteInstallerEngine.run("/usr/bin/codesign", ["--verify", "--deep", "--strict", payload.path])
            entries.append(PayloadEntry(name: definition.name, format: key.format, filename: key.filename,
                bundleIdentifier: definition.bundleIdentifier, identity: identity, files: try SuiteInstallerEngine.fileHashes(in: payload)))
        }
        let manifest = SuiteManifest(format: 2, version: suiteVersion, items: entries)
        let engine = SuiteInstallerEngine(payloadRoot: payloadRoot, manifest: manifest,
            locations: .inside(URL(fileURLWithPath: "/unused")), runningHosts: { _ in [] })
        try engine.inspectPayloads()
        let encoder = JSONEncoder(); encoder.outputFormatting = [.prettyPrinted, .sortedKeys]
        try encoder.encode(manifest).write(to: URL(fileURLWithPath: manifestPath), options: .atomic)
        print("Suite \(manifest.version) manifest created; identities and SHA-256 hashes captured for all \(entries.count) VST3 and AU bundles.")
    case .checkWhatsNew(let path):
        let notes = try WhatsNew.parse(Data(contentsOf: URL(fileURLWithPath: path)),
                                       requiredNames: SuiteCatalog.plugins.map(\.name), allowOtherNames: false)
        print("What’s new is valid: \(notes.values.map(\.count).reduce(0, +)) lines for \(notes.count) plug-ins.")
    case .inspectPayload:
        guard let resources = Bundle.main.resourceURL else { throw InstallError.message("Run this command inside the built installer app.") }
        let manifest = try SuiteInstallerEngine.readManifest(resources.appendingPathComponent("suite-manifest.json"))
        let engine = SuiteInstallerEngine(payloadRoot: resources.appendingPathComponent("Payload"), manifest: manifest,
            locations: .inside(URL(fileURLWithPath: "/unused")), runningHosts: { _ in [] })
        try engine.inspectPayloads()
        for key in SuiteCatalog.allItems {
            let entry = manifest.entry(key)
            print("\(key.title) \(entry.identity.version): SHA-256, bundle identity, \(key.format == .vst3 ? "VST3 class IDs" : "Audio Unit codes"), permissions and signature verified.")
            if key.format == .vst3 {
                print("  component \(entry.identity.componentID ?? "?"); controller \(entry.identity.controllerID ?? "?")")
            } else {
                print("  AudioComponents \((entry.identity.audioUnits ?? []).map(\.description).joined(separator: ", "))")
            }
        }
        let whatsNew = resources.appendingPathComponent("whats-new.json")
        _ = try WhatsNew.parse(Data(contentsOf: whatsNew), requiredNames: SuiteCatalog.plugins.map(\.name), allowOtherNames: false)
        print("What’s new notes verified.")
    case .renderPreview(let path, let state):
        _ = NSApplication.shared
        try InstallerDelegate().renderPreview(to: URL(fileURLWithPath: path), state: state)
    case .checkHosts:
        let hosts = InstallerDelegate.runningHosts(formats: Set(PluginFormat.allCases)).sorted()
        print(hosts.isEmpty ? "No audio hosts are blocking the suite." : "Quit before installing: \(hosts.joined(separator: ", "))")
        exit(hosts.isEmpty ? 0 : 2)
    case .gui:
        let application = NSApplication.shared
        let delegate = InstallerDelegate()
        application.delegate = delegate; application.setActivationPolicy(.regular); application.run()
        withExtendedLifetime(delegate) {}
    case .invalid:
        throw InstallError.message("Options: --inspect-payload, --self-test, --check-hosts, --check-whats-new <json>, --render-preview <png> [main|uninstall|result|repair], --make-manifest <payload-dir> <manifest-file> <suite-version>.")
    }
} catch {
    fputs("Vivid Fever Dreams Suite installer: \(UserFacingText.logText(for: error))\n", stderr)
    exit(1)
}
