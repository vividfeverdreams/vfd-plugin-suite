import Foundation
import Darwin

func runInstallerTests() throws {
    let fm = FileManager.default
    guard let canonicalTemp = realpath(fm.temporaryDirectory.path, nil) else { throw InstallError.message("Cannot resolve temporary test directory.") }
    let root = URL(fileURLWithPath: String(cString: canonicalTemp)).appendingPathComponent("vfd-suite-tests-\(UUID().uuidString)")
    free(canonicalTemp)
    try fm.createDirectory(at: root, withIntermediateDirectories: true)
    defer { try? fm.removeItem(at: root) }
    var checks = 0
    func expect(_ condition: @autoclosure () throws -> Bool, _ description: String) throws {
        guard try condition() else { throw InstallError.message("SELF-TEST FAILED: \(description)") }
        checks += 1; print("PASS: \(description)")
    }
    func rejects(_ description: String, _ operation: () throws -> Void) throws {
        var rejected = false
        do { try operation() } catch { rejected = true }
        try expect(rejected, description)
    }
    func bundle(at url: URL, index: Int, version: String) throws {
        let definition = SuiteCatalog.plugins[index]
        let contents = url.appendingPathComponent("Contents")
        try fm.createDirectory(at: contents.appendingPathComponent("MacOS"), withIntermediateDirectories: true)
        try fm.createDirectory(at: contents.appendingPathComponent("Resources"), withIntermediateDirectories: true)
        try fm.copyItem(at: URL(fileURLWithPath: "/usr/bin/true"), to: contents.appendingPathComponent("MacOS/\(definition.name)"))
        let info: [String: Any] = ["CFBundleIdentifier": definition.bundleIdentifier,
            "CFBundleExecutable": definition.name, "CFBundlePackageType": "BNDL",
            "CFBundleVersion": version, "CFBundleShortVersionString": version, "CFBundleName": definition.name]
        try PropertyListSerialization.data(fromPropertyList: info, format: .xml, options: 0).write(to: contents.appendingPathComponent("Info.plist"))
        let component = String(format: "%032X", index * 2 + 1)
        let controller = String(format: "%032X", index * 2 + 2)
        let module = """
        { "Classes": [
          { "CID": "\(component)", "Category": "Audio Module Class", },
          { "CID": "\(controller)", "Category": "Component Controller Class", },
        ], }
        """
        try module.write(to: contents.appendingPathComponent("Resources/moduleinfo.json"), atomically: true, encoding: .utf8)
        try SuiteInstallerEngine.run("/usr/bin/codesign", ["--force", "--sign", "-", url.path])
    }
    let payloadRoot = root.appendingPathComponent("Payload")
    let oldRoot = root.appendingPathComponent("Old")
    try fm.createDirectory(at: payloadRoot, withIntermediateDirectories: true)
    try fm.createDirectory(at: oldRoot, withIntermediateDirectories: true)
    var entries: [PayloadManifest] = []
    for (index, definition) in SuiteCatalog.plugins.enumerated() {
        let payload = payloadRoot.appendingPathComponent(definition.filename)
        try bundle(at: payload, index: index, version: definition.version)
        try bundle(at: oldRoot.appendingPathComponent(definition.filename), index: index, version: "0.0.1")
        entries.append(PayloadManifest(name: definition.name, filename: definition.filename, bundleIdentifier: definition.bundleIdentifier,
            identity: try SuiteInstallerEngine.identity(at: payload, definition: definition), files: try SuiteInstallerEngine.fileHashes(in: payload)))
    }
    let manifest = SuiteManifest(format: 1, version: "1.2.2", plugins: entries)
    var scenarioIndex = 0
    func scenario(existing: Set<Int> = []) throws -> SuiteInstallerEngine {
        scenarioIndex += 1
        let folder = root.appendingPathComponent("Scenario-\(scenarioIndex)")
        let destination = folder.appendingPathComponent("User/Library/Audio/Plug-Ins/VST3")
        try fm.createDirectory(at: destination, withIntermediateDirectories: true)
        for index in existing {
            let name = SuiteCatalog.plugins[index].filename
            try fm.copyItem(at: oldRoot.appendingPathComponent(name), to: destination.appendingPathComponent(name))
        }
        return SuiteInstallerEngine(payloadRoot: payloadRoot, manifest: manifest, destinationRoot: destination,
            systemRoot: folder.appendingPathComponent("System/Library/Audio/Plug-Ins/VST3"), runningHosts: { [] })
    }
    func state(_ engine: SuiteInstallerEngine) throws -> [String: [String: String]] {
        var result: [String: [String: String]] = [:]
        for definition in SuiteCatalog.plugins {
            let file = engine.destinationRoot.appendingPathComponent(definition.filename)
            if SuiteInstallerEngine.exists(file) { result[definition.filename] = try SuiteInstallerEngine.fileHashes(in: file) }
        }
        return result
    }
    func clean(_ engine: SuiteInstallerEngine) throws -> Bool {
        try !fm.contentsOfDirectory(atPath: engine.destinationRoot.path).contains { $0.hasPrefix(".vfd-suite-transaction-") }
    }
    let initial = try scenario()
    let preset = initial.destinationRoot.deletingLastPathComponent().appendingPathComponent("UserPresets/keep.json")
    try fm.createDirectory(at: preset.deletingLastPathComponent(), withIntermediateDirectories: true)
    try Data("a saved sound".utf8).write(to: preset)
    let install = try initial.install()
    try expect(install.installed == SuiteCatalog.plugins.count && install.updated == 0, "all five initial installations succeed together")
    for (index, entry) in entries.enumerated() {
        try initial.verifyPayload(at: initial.destinationRoot.appendingPathComponent(entry.filename), index: index)
        try expect(try SuiteInstallerEngine.fileHashes(in: initial.destinationRoot.appendingPathComponent(entry.filename)) == entry.files,
                   "\(entry.name) installed bytes match payload")
    }
    try expect(try String(contentsOf: preset) == "a saved sound", "user presets remain unchanged")
    let reinstall = try initial.install()
    try expect(reinstall.installed == 0 && reinstall.updated == SuiteCatalog.plugins.count, "same-version suite reinstall succeeds")
    try expect(try clean(initial), "successful suite transaction removes its staging folder")
    let mixed = try scenario(existing: [0, 2])
    let mixedResult = try mixed.install()
    try expect(mixedResult.installed == 3 && mixedResult.updated == 2, "mixed new/update suite commits all five")
    // Every possible partial commit is exercised for new, update, and mixed suites.
    for existing: Set<Int> in [[], [0, 1, 2, 3, 4], [0, 2]] {
        for commit in 1...SuiteCatalog.plugins.count {
            let engine = try scenario(existing: existing)
            let before = try state(engine)
            engine.failAfterCommitForTesting = commit
            try rejects("fault after commit \(commit), \(existing.count) existing bundles, rejects installation") { _ = try engine.install() }
            try expect(try state(engine) == before, "fault \(commit), \(existing.count) existing bundles, restores complete prior suite")
            try expect(try clean(engine), "fault \(commit), \(existing.count) existing bundles, cleans transaction files")
        }
    }
    let newer = try scenario(existing: [0, 1, 2, 3])
    try bundle(at: newer.destinationRoot.appendingPathComponent("ENERGY HARVESTER.vst3"), index: 4, version: "99.0.0")
    let newerState = try state(newer)
    try rejects("last payload's newer installed version blocks entire suite") { _ = try newer.install() }
    try expect(try state(newer) == newerState, "downgrade refusal leaves all five bundles intact")
    let tampered = try scenario(existing: [0, 2])
    let tamperedState = try state(tampered)
    let extra = payloadRoot.appendingPathComponent("ENERGY HARVESTER.vst3/Contents/Resources/tampered.txt")
    try Data("extra".utf8).write(to: extra)
    try rejects("last embedded payload corruption blocks all commits") { _ = try tampered.install() }
    try expect(try state(tampered) == tamperedState, "payload corruption preserves every installed bundle")
    try fm.removeItem(at: extra)
    let system = try scenario(existing: [0])
    try fm.createDirectory(at: system.systemRoot, withIntermediateDirectories: true)
    try fm.copyItem(at: oldRoot.appendingPathComponent("SHIFTY.vst3"), to: system.systemRoot.appendingPathComponent("Renamed SHIFTY.vst3"))
    let systemState = try state(system)
    try rejects("renamed system copy is detected by bundle identifier") { _ = try system.install() }
    try expect(try state(system) == systemState, "system duplicate refusal changes no installed bundles")
    let renamed = try scenario()
    try fm.copyItem(at: oldRoot.appendingPathComponent("SHIFTY.vst3"), to: renamed.destinationRoot.appendingPathComponent("Old SHIFTY.vst3"))
    try rejects("renamed per-user copy is blocked") { _ = try renamed.install() }
    let unrelated = try scenario(existing: [1])
    let plist = unrelated.destinationRoot.appendingPathComponent("THROW-UP.vst3/Contents/Info.plist")
    var object = try PropertyListSerialization.propertyList(from: Data(contentsOf: plist), format: nil) as! [String: Any]
    object["CFBundleIdentifier"] = "com.unrelated.plugin"
    try PropertyListSerialization.data(fromPropertyList: object, format: .xml, options: 0).write(to: plist)
    let unrelatedState = try state(unrelated)
    try rejects("unrelated item at target name is blocked") { _ = try unrelated.install() }
    try expect(try state(unrelated) == unrelatedState, "unrelated target remains unchanged")
    let wrongID = try scenario(existing: [3])
    let module = wrongID.destinationRoot.appendingPathComponent("SHIFTY.vst3/Contents/Resources/moduleinfo.json")
    try String(contentsOf: module).replacingOccurrences(of: entries[3].identity.componentID, with: String(repeating: "A", count: 32))
        .write(to: module, atomically: true, encoding: .utf8)
    let wrongIDState = try state(wrongID)
    try rejects("changed VST3 component identity blocks updates") { _ = try wrongID.install() }
    try expect(try state(wrongID) == wrongIDState, "incompatible installed VST3 identity remains untouched")
    let link = try scenario()
    try fm.createSymbolicLink(at: link.destinationRoot.appendingPathComponent("SHIFTY.vst3"), withDestinationURL: oldRoot.appendingPathComponent("SHIFTY.vst3"))
    try rejects("symlink destination is refused") { _ = try link.install() }
    let alias = root.appendingPathComponent("Alias")
    try fm.createSymbolicLink(at: alias, withDestinationURL: initial.destinationRoot)
    let linked = SuiteInstallerEngine(payloadRoot: payloadRoot, manifest: manifest, destinationRoot: alias,
                                     systemRoot: root.appendingPathComponent("NoSystem"), runningHosts: { [] })
    try rejects("symlink destination ancestor is refused") { _ = try linked.install() }
    let hostBase = try scenario(existing: [0, 2])
    let hostState = try state(hostBase)
    var hostChecks = 0
    let lateHost = SuiteInstallerEngine(payloadRoot: payloadRoot, manifest: manifest, destinationRoot: hostBase.destinationRoot,
        systemRoot: hostBase.systemRoot, runningHosts: { hostChecks += 1; return hostChecks > 1 ? ["Test Audio Host"] : [] })
    try rejects("host starting after preparation blocks commit") { _ = try lateHost.install() }
    try expect(try state(lateHost) == hostState, "late host refusal restores original suite state")
    let race = try scenario(existing: [0, 2])
    let changedFile = race.destinationRoot.appendingPathComponent("YOI-BOI.vst3/Contents/Resources/external.txt")
    race.afterStagingForTesting = { try Data("independent edit".utf8).write(to: changedFile) }
    try rejects("installed bundle mutation during staging blocks commit") { _ = try race.install() }
    try expect(try String(contentsOf: changedFile) == "independent edit", "independent destination edit is preserved")
    let interrupted = try scenario()
    let stale = interrupted.destinationRoot.appendingPathComponent(".vfd-suite-transaction-interrupted")
    try fm.createDirectory(at: stale, withIntermediateDirectories: false)
    try rejects("unfinished transaction blocks another installation") { _ = try interrupted.install() }
    try expect(SuiteInstallerEngine.exists(stale), "unfinished recovery data is retained")
    let stagedTamper = try scenario(existing: [1])
    let stagedState = try state(stagedTamper)
    stagedTamper.afterStagingForTesting = {
        let directory = try fm.contentsOfDirectory(at: stagedTamper.destinationRoot, includingPropertiesForKeys: nil)
            .first { $0.lastPathComponent.hasPrefix(".vfd-suite-transaction-") }!
        try Data("unexpected staged change".utf8).write(to: directory.appendingPathComponent("SHIFTY.vst3/Contents/Resources/changed.txt"))
    }
    try rejects("staged payload corruption rejects transaction") { _ = try stagedTamper.install() }
    try expect(try state(stagedTamper) == stagedState, "late staged corruption rolls back all previously committed bundles")
    try expect(!InstallerHostDetection.isAudioApplication(bundleIdentifier: SuiteCatalog.installerIdentifier,
        name: "VFD Suite Installer", processID: 10, installerProcessID: 11), "suite installer is excluded from audio-host detection")
    try expect(InstallerHostDetection.isAudioApplication(bundleIdentifier: "com.ableton.live", name: nil,
        processID: 10, installerProcessID: 11), "known host detected by identifier")
    try expect(InstallerHostDetection.isAudioApplication(bundleIdentifier: nil, name: "pluginval",
        processID: 10, installerProcessID: 11), "plug-in validation host detected by name")
    try expect(InstallerHostDetection.fileUsers("p10\ncInstaller\np11\ncUnknown Host\n", excluding: [10]) == ["Unknown Host"],
               "open-file detection catches unknown hosts and excludes installers")
    print("Suite installer self-test passed: \(checks) checks. All fixture writes stayed inside \(root.path).")
}
