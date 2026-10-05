import Foundation
import Darwin

func runInstallerTests() throws {
    let fm = FileManager.default
    guard let canonicalTemp = realpath(fm.temporaryDirectory.path, nil) else { throw InstallError.message("Cannot resolve temporary test directory.") }
    let root = URL(fileURLWithPath: String(cString: canonicalTemp)).appendingPathComponent("vfd-suite-tests-\(UUID().uuidString)")
    free(canonicalTemp)
    try fm.createDirectory(at: root, withIntermediateDirectories: true)
    /// Undo the immutable flags and read-only folders some tests create, so the fixture can always be deleted.
    func makeRemovable(_ folder: URL) {
        let paths = [folder.path] + (fm.enumerator(atPath: folder.path)?.compactMap { $0 as? String }.map { folder.path + "/" + $0 } ?? [])
        for path in paths {
            _ = chflags(path, 0)
            var info = stat()
            if lstat(path, &info) == 0 && (info.st_mode & S_IFMT) == S_IFDIR { _ = chmod(path, 0o755) }
        }
    }
    defer { makeRemovable(root); try? fm.removeItem(at: root) }
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
    func rejectsWith(_ description: String, containing text: String, _ operation: () throws -> Void) throws {
        var message: String?
        do { try operation() } catch { message = UserFacingText.message(for: error) }
        try expect(message?.contains(text) == true, description)
    }
    func exists(_ url: URL) -> Bool { SuiteInstallerEngine.exists(url) }
    let all = Set(SuiteCatalog.allItems)
    let vst3Items = Set(SuiteCatalog.allItems.filter { $0.format == .vst3 })
    let auItems = Set(SuiteCatalog.allItems.filter { $0.format == .au })
    func key(_ plugin: Int, _ format: PluginFormat) -> ItemKey { ItemKey(plugin: plugin, format: format) }

    /// A fixture bundle in the given format: VST3 with moduleinfo class IDs, AU with AudioComponents entries.
    func bundle(at url: URL, _ key: ItemKey, version: String, audioUnit: AudioUnitIdentity? = nil) throws {
        let definition = key.definition
        let contents = url.appendingPathComponent("Contents")
        try fm.createDirectory(at: contents.appendingPathComponent("MacOS"), withIntermediateDirectories: true)
        try fm.createDirectory(at: contents.appendingPathComponent("Resources"), withIntermediateDirectories: true)
        try fm.copyItem(at: URL(fileURLWithPath: "/usr/bin/true"), to: contents.appendingPathComponent("MacOS/\(definition.name)"))
        var info: [String: Any] = ["CFBundleIdentifier": definition.bundleIdentifier,
            "CFBundleExecutable": definition.name, "CFBundlePackageType": "BNDL",
            "CFBundleVersion": version, "CFBundleShortVersionString": version, "CFBundleName": definition.name]
        if key.format == .au {
            let unit = audioUnit ?? definition.audioUnit
            info["AudioComponents"] = [["type": unit.type, "subtype": unit.subtype, "manufacturer": unit.manufacturer,
                                        "name": "VIVID FEVER DREAMS: \(definition.name)", "version": 1, "factoryFunction": "Factory"]]
        } else {
            let module = """
            { "Classes": [
              { "CID": "\(String(format: "%032X", key.plugin * 2 + 1))", "Category": "Audio Module Class", },
              { "CID": "\(String(format: "%032X", key.plugin * 2 + 2))", "Category": "Component Controller Class", },
            ], }
            """
            try module.write(to: contents.appendingPathComponent("Resources/moduleinfo.json"), atomically: true, encoding: .utf8)
        }
        try PropertyListSerialization.data(fromPropertyList: info, format: .xml, options: 0).write(to: contents.appendingPathComponent("Info.plist"))
        try SuiteInstallerEngine.run("/usr/bin/codesign", ["--force", "--sign", "-", url.path])
    }
    func setInfo(_ value: Any, for name: String, in bundle: URL) throws {
        let plist = bundle.appendingPathComponent("Contents/Info.plist")
        var object = try PropertyListSerialization.propertyList(from: Data(contentsOf: plist), format: nil) as? [String: Any] ?? [:]
        object[name] = value
        try PropertyListSerialization.data(fromPropertyList: object, format: .xml, options: 0).write(to: plist)
    }
    let payloadRoot = root.appendingPathComponent("Payload")
    let oldRoot = root.appendingPathComponent("Old")
    try fm.createDirectory(at: payloadRoot, withIntermediateDirectories: true)
    try fm.createDirectory(at: oldRoot, withIntermediateDirectories: true)
    var entries: [PayloadEntry] = []
    for item in SuiteCatalog.allItems {
        let payload = payloadRoot.appendingPathComponent(item.filename)
        try bundle(at: payload, item, version: item.definition.version)
        try bundle(at: oldRoot.appendingPathComponent(item.filename), item, version: "0.0.1")
        entries.append(PayloadEntry(name: item.definition.name, format: item.format, filename: item.filename,
            bundleIdentifier: item.definition.bundleIdentifier,
            identity: try SuiteInstallerEngine.identity(at: payload, definition: item.definition, format: item.format),
            files: try SuiteInstallerEngine.fileHashes(in: payload)))
    }
    let manifest = SuiteManifest(format: 2, version: "1.3.0", items: entries)
    let payloadHashes = Dictionary(uniqueKeysWithValues: entries.map { ($0.filename, $0.files) })
    var refreshCount = 0
    var scenarioIndex = 0
    var trashFolders: [ObjectIdentifier: URL] = [:]
    func makeEngine(_ locations: InstallLocations, payload: URL = payloadRoot,
                    hosts: @escaping (Set<PluginFormat>) -> [String] = { _ in [] }) -> SuiteInstallerEngine {
        let engine = SuiteInstallerEngine(payloadRoot: payload, manifest: manifest, locations: locations, runningHosts: hosts)
        let trash = locations.recovered.deletingLastPathComponent().deletingLastPathComponent().deletingLastPathComponent()
            .deletingLastPathComponent().appendingPathComponent("Trash")
        engine.refreshAudioUnits = { refreshCount += 1; return "Audio Unit refresh recorded by the self-test." }
        engine.moveToTrash = { url in
            try fm.createDirectory(at: trash, withIntermediateDirectories: true)
            let target = trash.appendingPathComponent("\(UUID().uuidString)-\(url.lastPathComponent)")
            try fm.moveItem(at: url, to: target)
            return target
        }
        trashFolders[ObjectIdentifier(engine)] = trash
        return engine
    }
    func scenario(existing: Set<ItemKey> = [], hosts: @escaping (Set<PluginFormat>) -> [String] = { _ in [] }) throws -> SuiteInstallerEngine {
        scenarioIndex += 1
        let locations = InstallLocations.inside(root.appendingPathComponent("Scenario-\(scenarioIndex)"))
        for item in existing {
            let folder = locations.destinationRoot(item.format)
            try fm.createDirectory(at: folder, withIntermediateDirectories: true)
            try fm.copyItem(at: oldRoot.appendingPathComponent(item.filename), to: folder.appendingPathComponent(item.filename))
        }
        try fm.createDirectory(at: locations.userVST3, withIntermediateDirectories: true)
        return makeEngine(locations, hosts: hosts)
    }
    func trash(_ engine: SuiteInstallerEngine) -> [URL] {
        guard let folder = trashFolders[ObjectIdentifier(engine)] else { return [] }
        return (try? fm.contentsOfDirectory(at: folder, includingPropertiesForKeys: nil)) ?? []
    }
    func state(_ engine: SuiteInstallerEngine) throws -> [String: [String: String]] {
        var result: [String: [String: String]] = [:]
        for item in SuiteCatalog.allItems {
            let file = engine.destinationURL(item)
            if exists(file) { result[item.filename] = try SuiteInstallerEngine.fileHashes(in: file) }
        }
        return result
    }
    func clean(_ engine: SuiteInstallerEngine) throws -> Bool {
        for format in PluginFormat.allCases {
            let folder = engine.locations.destinationRoot(format)
            guard exists(folder) else { continue }
            if try fm.contentsOfDirectory(atPath: folder.path).contains(where: {
                $0.hasPrefix(SuiteInstallerEngine.transactionPrefix) || $0.hasPrefix(SuiteInstallerEngine.discardPrefix)
            }) { return false }
        }
        return true
    }
    func transactionFolder(_ engine: SuiteInstallerEngine) throws -> URL? {
        for format in PluginFormat.allCases.sorted() {
            let folder = engine.locations.destinationRoot(format)
            guard exists(folder) else { continue }
            if let found = try fm.contentsOfDirectory(at: folder, includingPropertiesForKeys: nil)
                .first(where: { $0.lastPathComponent.hasPrefix(SuiteInstallerEngine.transactionPrefix) }) { return found }
        }
        return nil
    }
    func isSubsequence(_ wanted: [String], of states: [String]) -> Bool {
        var remaining = wanted[...]
        for state in states where state == remaining.first { remaining = remaining.dropFirst() }
        return remaining.isEmpty
    }

    // MARK: Complete transaction over the selection

    let initial = try scenario()
    let preset = initial.locations.userVST3.deletingLastPathComponent().appendingPathComponent("UserPresets/keep.json")
    try fm.createDirectory(at: preset.deletingLastPathComponent(), withIntermediateDirectories: true)
    try Data("a saved sound".utf8).write(to: preset)
    let first = try initial.install(selection: all)
    try expect(first.installed.count == 10 && first.updated.isEmpty && first.skipped.isEmpty, "all ten VST3 and AU bundles install together")
    for item in SuiteCatalog.allItems {
        try initial.verifyPayload(at: initial.destinationURL(item), key: item)
        try expect(try SuiteInstallerEngine.fileHashes(in: initial.destinationURL(item)) == manifest.entry(item).files,
                   "\(item.title) installed bytes match payload")
    }
    try expect(exists(initial.locations.userComponents.appendingPathComponent("YOI-BOI.component")) && refreshCount == 1 && first.audioUnitsRefreshed,
               "Audio Units go to the Components folder and the AU list is refreshed once")
    try expect(try String(contentsOf: preset) == "a saved sound", "user presets remain unchanged")
    let again = try initial.install(selection: all)
    try expect(again.skipped.count == 10 && again.installed.isEmpty && again.updated.isEmpty, "a repeat install skips every bundle whose bytes already match")
    try expect(refreshCount == 1 && !again.audioUnitsRefreshed && (try clean(initial)), "nothing is refreshed or staged when nothing changed")
    try expect(first.warnings.isEmpty && again.warnings.isEmpty, "a normal installation reports no warnings")
    let mixed = try scenario(existing: [key(0, .vst3), key(2, .vst3), key(1, .au)])
    let mixedResult = try mixed.install(selection: all)
    try expect(mixedResult.installed.count == 7 && mixedResult.updated.count == 3, "mixed new/update selection commits all ten")

    // MARK: Status shown beside each checkbox

    let status = try scenario(existing: [key(0, .vst3), key(1, .au)])
    var states = status.states()
    try expect(states[key(0, .vst3)] == .older("0.0.1") && states[key(1, .au)] == .older("0.0.1") && states[key(2, .vst3)] == .notInstalled,
               "status reports older and missing items")
    try expect(states[key(0, .vst3)]?.label(newVersion: "0.2.0") == "Installed 0.0.1 → 0.2.0" && ItemState.notInstalled.label(newVersion: "0.2.0") == "New",
               "status labels read “Installed 0.0.1 → 0.2.0” and “New”")
    _ = try status.install(selection: [key(0, .vst3)])
    try Data("extra".utf8).write(to: status.destinationURL(key(1, .au)).appendingPathComponent("Contents/Resources/extra.txt"))
    states = status.states()
    try expect(states[key(0, .vst3)] == .upToDate && states[key(0, .vst3)]?.label(newVersion: "0.2.0") == "Up to date", "an identical install reads “Up to date”")
    try expect(states[key(1, .au)] == .older("0.0.1"), "an older bundle with an extra file is still an update")
    let sameVersion = try scenario()
    _ = try sameVersion.install(selection: [key(3, .vst3)])
    try Data("extra".utf8).write(to: sameVersion.destinationURL(key(3, .vst3)).appendingPathComponent("Contents/Resources/extra.txt"))
    try expect(sameVersion.state(of: key(3, .vst3)) == .reinstall, "the same version with different bytes reads “Reinstall”")
    try bundle(at: sameVersion.destinationURL(key(4, .au)), key(4, .au), version: "9.9.9")
    try expect(sameVersion.state(of: key(4, .au)) == .newer("9.9.9") && ItemState.newer("9.9.9").blocksInstall, "a newer installed bundle blocks and is reported")
    try bundle(at: sameVersion.destinationURL(key(2, .vst3)), key(2, .vst3), version: "0.0.1")
    try setInfo("com.someone.else", for: "CFBundleIdentifier", in: sameVersion.destinationURL(key(2, .vst3)))
    if case .blocked = sameVersion.state(of: key(2, .vst3)) { try expect(true, "an unrelated bundle at the target name is reported as needing attention") }
    else { try expect(false, "an unrelated bundle at the target name is reported as needing attention") }

    // MARK: Selection subsets and skipping identical bytes

    let subset = try scenario()
    try fm.removeItem(at: subset.locations.userVST3)
    let refreshesBefore = refreshCount
    let subsetResult = try subset.install(selection: [key(0, .au), key(3, .au)])
    try expect(subsetResult.installed == [key(0, .au), key(3, .au)] && (try state(subset)).count == 2 && !exists(subset.locations.userVST3),
               "installing only two AUs touches only those two, without creating the VST3 folder")
    try expect(refreshCount == refreshesBefore + 1, "an AU-only install refreshes the Audio Unit list")
    let vstOnly = try scenario()
    _ = try vstOnly.install(selection: [key(2, .vst3)])
    try expect(refreshCount == refreshesBefore + 1 && !exists(vstOnly.locations.userComponents), "a VST3-only install neither refreshes AUs nor creates the Components folder")
    let partial = try scenario()
    _ = try partial.install(selection: all)
    try fm.removeItem(at: partial.destinationURL(key(3, .vst3)))
    try fm.copyItem(at: oldRoot.appendingPathComponent("SHIFTY.vst3"), to: partial.destinationURL(key(3, .vst3)))
    let partialResult = try partial.install(selection: all)
    try expect(partialResult.updated == [key(3, .vst3)] && partialResult.skipped.count == 9, "only the changed bundle is replaced; nine identical ones are skipped")
    try rejectsWith("an empty selection is refused", containing: "Select at least one") { _ = try partial.install(selection: []) }

    // Every possible partial commit is exercised for new, update, and mixed selections.
    for existing: Set<ItemKey> in [[], all, [key(0, .vst3), key(2, .vst3), key(1, .au)]] {
        for commit in 1...SuiteCatalog.allItems.count {
            let engine = try scenario(existing: existing)
            let before = try state(engine)
            var observed: [SuiteJournal] = []
            engine.journalObserverForTesting = { observed.append($0) }
            engine.failAfterCommitForTesting = commit
            try rejects("fault after commit \(commit), \(existing.count) existing bundles, rejects installation") { _ = try engine.install(selection: all) }
            try expect(try state(engine) == before, "fault \(commit), \(existing.count) existing bundles, restores the complete prior state")
            try expect(try clean(engine), "fault \(commit), \(existing.count) existing bundles, cleans transaction files")
            let items = observed.last?.items ?? []
            try expect(observed.last?.state == "rolled-back" && items.filter { $0.state == "restored" }.count == commit &&
                       items.filter { $0.state == "pending" }.count == SuiteCatalog.allItems.count - commit,
                       "fault \(commit), \(existing.count) existing bundles, journal records restored and pending items")
            let firstItemStates = observed.compactMap { $0.items?.first?.state }
            try expect(isSubsequence(["pending", "committing", "committed", "restoring", "restored"], of: firstItemStates),
                       "fault \(commit), \(existing.count) existing bundles, journal is written before each rename and rollback step")
        }
    }

    // MARK: Refusals

    let newer = try scenario(existing: all)
    try fm.removeItem(at: newer.destinationURL(key(4, .vst3)))
    try bundle(at: newer.destinationURL(key(4, .vst3)), key(4, .vst3), version: "99.0.0")
    let newerState = try state(newer)
    try rejectsWith("a newer installed bundle blocks the selection that includes it", containing: "Untick it") { _ = try newer.install(selection: all) }
    try expect(try state(newer) == newerState, "downgrade refusal leaves every bundle intact")
    let withoutNewer = try newer.install(selection: all.subtracting([key(4, .vst3)]))
    try expect(withoutNewer.updated.count == 9, "unticking the newer bundle installs the other nine")
    let tampered = try scenario(existing: [key(0, .vst3), key(2, .au)])
    let tamperedState = try state(tampered)
    let extra = payloadRoot.appendingPathComponent("ENERGY HARVESTER.component/Contents/Resources/tampered.txt")
    try Data("extra".utf8).write(to: extra)
    try rejects("last embedded payload corruption blocks all commits") { _ = try tampered.install(selection: all) }
    try expect(try state(tampered) == tamperedState, "payload corruption preserves every installed bundle")
    try expect((try tampered.install(selection: vst3Items)).installed.count == 4, "a corrupt AU payload does not block a VST3-only selection")
    try fm.removeItem(at: extra)
    let system = try scenario(existing: [key(0, .vst3)])
    try fm.createDirectory(at: system.locations.systemVST3, withIntermediateDirectories: true)
    try fm.copyItem(at: oldRoot.appendingPathComponent("SHIFTY.vst3"), to: system.locations.systemVST3.appendingPathComponent("Renamed SHIFTY.vst3"))
    let systemState = try state(system)
    try rejects("renamed system VST3 copy is detected by bundle identifier") { _ = try system.install(selection: all) }
    try expect(try state(system) == systemState, "system duplicate refusal changes no installed bundles")
    let systemAU = try scenario()
    try fm.createDirectory(at: systemAU.locations.systemComponents, withIntermediateDirectories: true)
    let systemAUCopy = systemAU.locations.systemComponents.appendingPathComponent("Old Throw.component")
    try fm.copyItem(at: oldRoot.appendingPathComponent("THROW-UP.component"), to: systemAUCopy)
    try setInfo("com.someone.repackaged", for: "CFBundleIdentifier", in: systemAUCopy)
    try rejectsWith("an AU duplicate in /Library/Audio/Plug-Ins/Components is detected by its AU codes", containing: "/Library/Audio/Plug-Ins/Components") {
        _ = try systemAU.install(selection: [key(1, .au)])
    }
    try expect((try systemAU.install(selection: [key(1, .vst3)])).installed.count == 1, "a system AU duplicate does not block the VST3 format")
    let renamed = try scenario()
    try fm.copyItem(at: oldRoot.appendingPathComponent("SHIFTY.vst3"), to: renamed.locations.userVST3.appendingPathComponent("Old SHIFTY.vst3"))
    try rejects("renamed per-user VST3 copy is blocked") { _ = try renamed.install(selection: all) }
    let renamedAU = try scenario()
    try fm.createDirectory(at: renamedAU.locations.userComponents, withIntermediateDirectories: true)
    try fm.copyItem(at: oldRoot.appendingPathComponent("SHIFTY.component"), to: renamedAU.locations.userComponents.appendingPathComponent("Old SHIFTY.component"))
    try rejects("renamed per-user AU copy is blocked") { _ = try renamedAU.install(selection: auItems) }
    let unrelated = try scenario(existing: [key(1, .vst3)])
    try setInfo("com.unrelated.plugin", for: "CFBundleIdentifier", in: unrelated.destinationURL(key(1, .vst3)))
    let unrelatedState = try state(unrelated)
    try rejects("unrelated item at target name is blocked") { _ = try unrelated.install(selection: all) }
    try expect(try state(unrelated) == unrelatedState, "unrelated target remains unchanged")
    let wrongID = try scenario(existing: [key(3, .vst3)])
    let module = wrongID.destinationURL(key(3, .vst3)).appendingPathComponent("Contents/Resources/moduleinfo.json")
    try String(contentsOf: module).replacingOccurrences(of: manifest.entry(key(3, .vst3)).identity.componentID ?? "", with: String(repeating: "A", count: 32))
        .write(to: module, atomically: true, encoding: .utf8)
    let wrongIDState = try state(wrongID)
    try rejects("changed VST3 component identity blocks updates") { _ = try wrongID.install(selection: all) }
    try expect(try state(wrongID) == wrongIDState, "incompatible installed VST3 identity remains untouched")
    let wrongAU = try scenario()
    try fm.createDirectory(at: wrongAU.locations.userComponents, withIntermediateDirectories: true)
    try bundle(at: wrongAU.destinationURL(key(3, .au)), key(3, .au), version: "0.0.1", audioUnit: AudioUnitIdentity(type: "aufx", subtype: "Zzz9", manufacturer: "Yoib"))
    try rejectsWith("an installed AU with different codes blocks updates", containing: "different Audio Unit codes") { _ = try wrongAU.install(selection: auItems) }
    try expect(exists(wrongAU.destinationURL(key(3, .au))), "the AU with different codes remains untouched")
    let noComponents = root.appendingPathComponent("NoComponents.component")
    try bundle(at: noComponents, key(2, .au), version: "0.11.0")
    try setInfo([[String: Any]](), for: "AudioComponents", in: noComponents)
    try rejects("an AU bundle without AudioComponents entries is not accepted") {
        _ = try SuiteInstallerEngine.identity(at: noComponents, definition: SuiteCatalog.plugins[2], format: .au)
    }
    var badItems = entries
    let badIndex = SuiteCatalog.index(of: key(2, .au))
    badItems[badIndex] = PayloadEntry(name: entries[badIndex].name, format: .au, filename: entries[badIndex].filename,
        bundleIdentifier: entries[badIndex].bundleIdentifier,
        identity: BundleIdentity(version: "0.11.0", audioUnits: [AudioUnitIdentity(type: "aufx", subtype: "Nope", manufacturer: "Yoib")]),
        files: entries[badIndex].files)
    try rejects("a manifest whose AU codes differ from the catalog is refused") {
        try SuiteInstallerEngine(payloadRoot: payloadRoot, manifest: SuiteManifest(format: 2, version: "1.3.0", items: badItems),
                                 locations: .inside(root.appendingPathComponent("Unused")), runningHosts: { _ in [] }).validateManifest()
    }
    let link = try scenario()
    try fm.createSymbolicLink(at: link.locations.userVST3.appendingPathComponent("SHIFTY.vst3"), withDestinationURL: oldRoot.appendingPathComponent("SHIFTY.vst3"))
    try rejects("symlink destination is refused") { _ = try link.install(selection: all) }
    var aliasLocations = InstallLocations.inside(root.appendingPathComponent("AliasScenario"))
    let realComponents = root.appendingPathComponent("RealComponents")
    try fm.createDirectory(at: realComponents, withIntermediateDirectories: true)
    try fm.createDirectory(at: aliasLocations.userComponents.deletingLastPathComponent(), withIntermediateDirectories: true)
    try fm.createSymbolicLink(at: aliasLocations.userComponents, withDestinationURL: realComponents)
    aliasLocations.userVST3 = root.appendingPathComponent("AliasScenarioVST3")
    let linked = makeEngine(aliasLocations)
    try rejects("a symlinked Components folder is refused") { _ = try linked.install(selection: auItems) }
    try expect(try fm.contentsOfDirectory(atPath: realComponents.path).isEmpty, "the symlink target receives no files")
    let hostBase = try scenario(existing: [key(0, .vst3), key(2, .vst3)])
    let hostState = try state(hostBase)
    var hostChecks = 0
    let lateHost = makeEngine(hostBase.locations, hosts: { _ in hostChecks += 1; return hostChecks > 1 ? ["Test Audio Host"] : [] })
    try rejects("host starting after preparation blocks commit") { _ = try lateHost.install(selection: all) }
    try expect(try state(lateHost) == hostState, "late host refusal restores original state")
    let logicRunning = try scenario(hosts: { formats in formats.contains(.au) ? ["Logic Pro"] : [] })
    try rejectsWith("Logic Pro blocks an AU install", containing: "Logic Pro") { _ = try logicRunning.install(selection: all) }
    try expect((try logicRunning.install(selection: vst3Items)).installed.count == 5, "Logic Pro does not block a VST3-only install")
    let race = try scenario(existing: [key(0, .vst3), key(2, .vst3)])
    let changedFile = race.destinationURL(key(0, .vst3)).appendingPathComponent("Contents/Resources/external.txt")
    race.afterStagingForTesting = { try Data("independent edit".utf8).write(to: changedFile) }
    try rejects("installed bundle mutation during staging blocks commit") { _ = try race.install(selection: all) }
    try expect(try String(contentsOf: changedFile) == "independent edit", "independent destination edit is preserved")
    let interrupted = try scenario()
    let stale = interrupted.locations.userVST3.appendingPathComponent(".vfd-suite-transaction-interrupted")
    try fm.createDirectory(at: stale, withIntermediateDirectories: false)
    try Data("unknown recovery data".utf8).write(to: stale.appendingPathComponent("notes.txt"))
    try rejectsWith("unfinished transaction without a journal blocks another installation and points to Repair", containing: "Repair") {
        _ = try interrupted.install(selection: all)
    }
    try expect(exists(stale.appendingPathComponent("notes.txt")), "unfinished recovery data is retained")
    let componentsLeftover = try scenario()
    try fm.createDirectory(at: componentsLeftover.locations.userComponents.appendingPathComponent(".vfd-suite-transaction-elsewhere"), withIntermediateDirectories: true)
    try Data("x".utf8).write(to: componentsLeftover.locations.userComponents.appendingPathComponent(".vfd-suite-transaction-elsewhere/notes.txt"))
    try rejects("an unfinished transaction in the Components folder blocks even a VST3-only install") { _ = try componentsLeftover.install(selection: vst3Items) }
    let stagedTamper = try scenario(existing: [key(1, .vst3)])
    let stagedState = try state(stagedTamper)
    stagedTamper.afterStagingForTesting = {
        guard let directory = try transactionFolder(stagedTamper) else { throw InstallError.message("No transaction folder") }
        let position = SuiteCatalog.index(of: key(3, .au))
        try Data("unexpected staged change".utf8).write(to: directory.appendingPathComponent("\(position).staged/Contents/Resources/changed.txt"))
    }
    try rejects("staged payload corruption rejects transaction") { _ = try stagedTamper.install(selection: all) }
    try expect(try state(stagedTamper) == stagedState, "late staged corruption rolls back all previously committed bundles")

    // MARK: Hosts

    func host(_ identifier: String?, _ name: String?, _ formats: Set<PluginFormat> = [.vst3]) -> Bool {
        InstallerHostDetection.isAudioApplication(bundleIdentifier: identifier, name: name, processID: 10, installerProcessID: 11, formats: formats)
    }
    try expect(!host(SuiteCatalog.installerIdentifier, "VFD Suite Installer") && host("com.ableton.live", nil) && host(nil, "pluginval"),
               "suite installer excluded; known hosts detected by identifier and name")
    try expect(InstallerHostDetection.fileUsers("p10\ncInstaller\np11\ncUnknown Host\n", excluding: [10]) == ["Unknown Host"],
               "open-file detection catches unknown hosts and excludes installers")
    try expect(!host("com.apple.logic10", "Logic Pro") && !host("com.apple.garageband10", "GarageBand") && !host("com.apple.mainstage3", "MainStage"),
               "Logic Pro, GarageBand and MainStage do not block a VST3-only change")
    try expect(host("com.apple.logic10", nil, [.au]) && host("com.apple.garageband10", nil, [.vst3, .au]) && host(nil, "MainStage", [.au]) && host(nil, "Logic Pro", [.au]),
               "Logic Pro, GarageBand and MainStage block any change that includes an AU")
    try expect(!host("com.avid.ProTools", "Pro Tools", [.au]) && !host("com.steinberg.activationmanager", "Steinberg Activation Manager", [.au])
               && !host("com.motu.cuemix5", "CueMix 5") && !host("com.image-line.flcloud", "FL Cloud Plugins"),
               "AAX-only hosts and vendor utilities are not mistaken for audio hosts")
    try expect(host("com.steinberg.cubase13", nil) && host("com.steinberg.nuendo13", nil) && host("com.presonus.studioone6", nil)
               && host("com.bitwig.BitwigStudio", nil) && host("com.cockos.reaper", nil) && host("com.image-line.flstudio", nil)
               && host("com.Tracktion.pluginval", nil) && host("com.cycling74.Max", nil) && host("com.motu.DigitalPerformer", nil),
               "VST3 hosts are detected by bundle identifier")
    try expect(host(nil, "Gig Performer 5") && host(nil, "Maschine 2") && host(nil, "Vienna Ensemble Pro 7") && host(nil, "Renoise")
               && host(nil, "Komplete Kontrol") && host(nil, "Reason 12"), "VST3 hosts without a listed identifier are detected by name")
    try expect(!host(nil, "Element") && !host(nil, "Reason Studios Companion") && !host(nil, "Maxwell") && !host(nil, "Logic Pro X Tutorials", [.au]),
               "name matching does not catch unrelated apps")
    try expect(InstallerHostDetection.isInstaller("com.yoiboi.energyharvester.installer") && InstallerHostDetection.isInstaller("COM.YOIBOI.SHIFTY.INSTALLER")
               && InstallerHostDetection.isInstaller("com.yoiboi.installer") && !InstallerHostDetection.isInstaller("com.yoiboi.synth"),
               "every Vivid Fever Dreams installer, including ENERGY HARVESTER's, is recognised")

    // MARK: Open-file host detection runs lsof for real

    let heldFile = root.appendingPathComponent("held-by-child.bin")
    let unopenedFile = root.appendingPathComponent("never-opened.bin")
    try Data("held".utf8).write(to: heldFile)
    try Data("idle".utf8).write(to: unopenedFile)
    let holder = Process()
    holder.executableURL = URL(fileURLWithPath: "/usr/bin/tail")
    holder.arguments = ["-f", heldFile.path]
    holder.standardOutput = FileHandle.nullDevice
    try holder.run()
    var users: [String] = []
    for _ in 0..<50 {
        users = InstallerHostDetection.openFileUsers(paths: [heldFile.path, unopenedFile.path], excluding: [])
        if !users.isEmpty { break }
        usleep(100_000)
    }
    let excludedHolder = InstallerHostDetection.openFileUsers(paths: [heldFile.path, unopenedFile.path], excluding: [holder.processIdentifier])
    holder.terminate(); holder.waitUntilExit()
    try expect(users.contains("tail"), "open-file check reports a process holding one path while another listed path is not open (lsof status 1)")
    try expect(!excludedHolder.contains("tail"), "open-file check skips excluded process IDs")
    try expect(InstallerHostDetection.openFileUsers(paths: [unopenedFile.path], excluding: []).isEmpty, "paths nobody has open report no host")
    let ownPID = ProcessInfo.processInfo.processIdentifier
    let ownHandle = try FileHandle(forReadingFrom: unopenedFile)
    let ownSeen = !InstallerHostDetection.openFileUsers(paths: [unopenedFile.path], excluding: []).isEmpty
    let ownExcluded = InstallerHostDetection.openFileUsers(paths: [unopenedFile.path], excluding: [ownPID]).isEmpty
    try ownHandle.close()
    try expect(ownSeen && ownExcluded, "the installer's own open files are excluded by its process ID")

    // MARK: Quarantine is never copied into the plug-in folders

    func allPaths(_ folder: URL) -> [String] {
        [folder.path] + (fm.enumerator(atPath: folder.path)?.compactMap { $0 as? String }.map { folder.path + "/" + $0 } ?? [])
    }
    func hasQuarantine(_ path: String) -> Bool { getxattr(path, "com.apple.quarantine", nil, 0, 0, XATTR_NOFOLLOW) >= 0 }
    let quarantinedPayload = root.appendingPathComponent("QuarantinedPayload")
    try fm.copyItem(at: payloadRoot, to: quarantinedPayload)
    let flag = Array("0083;00000000;Safari;".utf8)
    for path in allPaths(quarantinedPayload) {
        guard flag.withUnsafeBytes({ setxattr(path, "com.apple.quarantine", $0.baseAddress, $0.count, 0, XATTR_NOFOLLOW) }) == 0 else {
            throw InstallError.message("Cannot set the fixture quarantine flag on \(path)")
        }
    }
    let quarantineTarget = try scenario(existing: [key(1, .vst3)])
    let quarantineEngine = makeEngine(quarantineTarget.locations, payload: quarantinedPayload)
    let quarantineResult = try quarantineEngine.install(selection: all)
    let installedPaths = SuiteCatalog.allItems.flatMap { allPaths(quarantineTarget.destinationURL($0)) }
    try expect(quarantineResult.installed.count == 9 && allPaths(quarantinedPayload).allSatisfy(hasQuarantine),
               "a fully quarantined payload installs, and the payload itself keeps its flags")
    try expect(installedPaths.count > 20 && !installedPaths.contains(where: hasQuarantine), "installed VST3 and AU bundles never inherit the quarantine flag")

    // MARK: Launch arguments

    try expect(LaunchMode.parse(["app"]) == .gui && LaunchMode.parse(["app", "-psn_0_1234567"]) == .gui
               && LaunchMode.parse(["app", "-NSDocumentRevisionsDebugMode", "YES"]) == .gui, "the window opens even when macOS adds -psn_ or runtime arguments")
    try expect(LaunchMode.parse(["app", "-psn_0_1", "--self-test"]) == .selfTest, "known options still work beside a -psn_ argument")
    try expect(LaunchMode.parse(["app", "--make-manifest", "a", "b", "1.3.0"]) == .makeManifest(payload: "a", manifest: "b", version: "1.3.0")
               && LaunchMode.parse(["app", "--check-hosts"]) == .checkHosts && LaunchMode.parse(["app", "--check-whats-new", "n.json"]) == .checkWhatsNew("n.json")
               && LaunchMode.parse(["app", "--render-preview", "p.png", "repair"]) == .renderPreview("p.png", state: "repair")
               && LaunchMode.parse(["app", "--render-preview", "p.png"]) == .renderPreview("p.png", state: "main"), "build and diagnostic options parse")
    try expect(LaunchMode.parse(["app", "--bogus"]) == .invalid && LaunchMode.parse(["app", "--self-test", "extra"]) == .invalid
               && LaunchMode.parse(["app", "--render-preview", "p.png", "sideways"]) == .invalid, "unknown or malformed -- options are still rejected")

    // MARK: Interrupted transactions are cleaned up only when provably safe

    func snapshots(_ engine: SuiteInstallerEngine, _ keys: [ItemKey], withDestinations: Bool) throws -> [SuiteSnapshot] {
        try keys.map { item in
            let url = engine.destinationURL(item)
            let destination = withDestinations ? url.path : nil
            guard exists(url) else { return SuiteSnapshot(filename: item.filename, identity: nil, hashes: nil, destination: destination) }
            return SuiteSnapshot(filename: item.filename, identity: try SuiteInstallerEngine.identity(at: url, definition: item.definition, format: item.format),
                                 hashes: try SuiteInstallerEngine.fileHashes(in: url), destination: destination)
        }
    }
    let vst3Order = SuiteCatalog.allItems.filter { $0.format == .vst3 }
    /// format nil writes the format-1 journal of suite 1.2.2; format 3 is the current journal over both folders.
    func leftover(in engine: SuiteInstallerEngine, state: String, format: Int?, payload: Bool, keys: [ItemKey]? = nil) throws -> URL {
        let folder = engine.locations.userVST3.appendingPathComponent(SuiteInstallerEngine.transactionPrefix + UUID().uuidString)
        try fm.createDirectory(at: folder, withIntermediateDirectories: false)
        let covered = keys ?? vst3Order
        let hashes = payload ? Dictionary(uniqueKeysWithValues: covered.map { ($0.filename, manifest.entry($0).files) }) : nil
        let journal = SuiteJournal(format: format, state: state, destination: engine.locations.userVST3.path, suiteVersion: nil,
                                   previous: try snapshots(engine, covered, withDestinations: (format ?? 1) >= 3), payload: hashes, items: nil, committed: [])
        try JSONEncoder().encode(journal).write(to: folder.appendingPathComponent("transaction.json"))
        try fm.copyItem(at: payloadRoot.appendingPathComponent("YOI-BOI.vst3"), to: folder.appendingPathComponent("0.staged"))
        return folder
    }
    let safeStaging = try scenario(existing: [key(0, .vst3), key(2, .vst3)])
    let stagingFolder = try leftover(in: safeStaging, state: "staging", format: 3, payload: true, keys: SuiteCatalog.allItems)
    let afterCleanup = try safeStaging.install(selection: all)
    try expect(!exists(stagingFolder) && (try clean(safeStaging)), "an interrupted staging transaction that changed nothing is cleaned up automatically")
    try expect(afterCleanup.installed.count == 8 && afterCleanup.updated.count == 2, "installation proceeds after the automatic cleanup")
    let legacyPrepared = try scenario(existing: [key(1, .vst3)])
    let legacyFolder = try leftover(in: legacyPrepared, state: "prepared", format: nil, payload: false)
    _ = try legacyPrepared.install(selection: all)
    try expect(!exists(legacyFolder), "a suite 1.2.2 journal left in the prepared state is recognised and cleaned up")
    let changedSince = try scenario(existing: [key(0, .vst3)])
    let changedFolder = try leftover(in: changedSince, state: "prepared", format: nil, payload: true)
    try Data("changed after the crash".utf8).write(to: changedSince.destinationURL(key(0, .vst3)).appendingPathComponent("Contents/Resources/later.txt"))
    let changedState = try state(changedSince)
    try rejectsWith("an interrupted transaction whose destination changed keeps blocking and names its folder", containing: changedFolder.lastPathComponent) {
        _ = try changedSince.install(selection: all)
    }
    try expect(exists(changedFolder.appendingPathComponent("0.staged")) && (try state(changedSince)) == changedState,
               "the blocking transaction and the installed plug-ins are left untouched")
    let finished = try scenario()
    _ = try finished.install(selection: all)
    let finishedFolder = try leftover(in: finished, state: "all-verified", format: 3, payload: true, keys: SuiteCatalog.allItems)
    _ = try finished.install(selection: all)
    try expect(!exists(finishedFolder), "a transaction that had already verified all ten is cleaned up")
    let legacyFinished = try scenario()
    _ = try legacyFinished.install(selection: all)
    let legacyFinishedFolder = try leftover(in: legacyFinished, state: "all-verified", format: nil, payload: false)
    try rejects("an all-verified journal without payload hashes is not assumed safe") { _ = try legacyFinished.install(selection: all) }
    try expect(exists(legacyFinishedFolder), "that unproven transaction is retained")
    let committingLeftover = try scenario(existing: [key(3, .vst3)])
    let committingFolder = try leftover(in: committingLeftover, state: "committing", format: 3, payload: true)
    try rejects("a transaction interrupted while committing keeps blocking") { _ = try committingLeftover.install(selection: all) }
    try expect(exists(committingFolder), "the committing transaction is retained")
    let emptyLeftover = try scenario()
    let emptyFolder = emptyLeftover.locations.userVST3.appendingPathComponent(SuiteInstallerEngine.transactionPrefix + "EMPTY")
    try fm.createDirectory(at: emptyFolder, withIntermediateDirectories: false)
    let discardLeftover = emptyLeftover.locations.userVST3.appendingPathComponent(SuiteInstallerEngine.discardPrefix + "OLD")
    try fm.createDirectory(at: discardLeftover.appendingPathComponent("0.staged"), withIntermediateDirectories: true)
    _ = try emptyLeftover.install(selection: all)
    try expect(!exists(emptyFolder) && !exists(discardLeftover) && (try clean(emptyLeftover)),
               "empty transaction folders and finished discard folders are removed")
    let stuck = try scenario(existing: [key(3, .vst3)])
    stuck.afterStagingForTesting = {
        guard let folder = try transactionFolder(stuck) else { throw InstallError.message("No transaction folder") }
        let file = folder.appendingPathComponent("locked.txt")
        try Data("immutable".utf8).write(to: file)
        guard chflags(file.path, 0x2) == 0 else { throw InstallError.message("Cannot set the user-immutable flag") } // UF_IMMUTABLE
    }
    let stuckResult = try stuck.install(selection: all)
    try expect(stuckResult.warnings.count == 1 && stuckResult.warnings[0].contains(SuiteInstallerEngine.discardPrefix),
               "a temporary folder that cannot be deleted is reported as a warning instead of a silent success")
    try expect(try state(stuck) == payloadHashes, "the installation itself still completed")
    makeRemovable(stuck.locations.userVST3)
    stuck.afterStagingForTesting = nil
    _ = try stuck.install(selection: all)
    try expect(try clean(stuck), "the next installation removes the leftover temporary folder")

    // MARK: The retained journal says exactly which item needs recovery

    let broken = try scenario(existing: [key(0, .vst3), key(2, .vst3)])
    broken.failAfterCommitForTesting = 2
    broken.afterCommitForTesting = { count in
        if count == 2 { try Data("edited by another program".utf8).write(to: broken.destinationURL(key(0, .vst3)).appendingPathComponent("Contents/Resources/outside.txt")) }
    }
    try rejectsWith("a rollback that cannot safely restore one plug-in reports recovery", containing: "could not fully restore") { _ = try broken.install(selection: all) }
    let retained = try transactionFolder(broken)
    let recorded = retained.flatMap { SuiteInstallerEngine.readJournal($0) }
    let recordedStates = recorded?.items?.map(\.state) ?? []
    try expect(recorded?.state == "recovery-required" && Array(recordedStates.prefix(3)) == ["rollback-failed", "restored", "pending"]
               && recordedStates.count == 10 && recorded?.items?[0].destination == broken.destinationURL(key(0, .vst3)).path,
               "the retained journal records each item's state and destination")
    try expect(retained.map { exists($0.appendingPathComponent("0.staged")) && !exists($0.appendingPathComponent("1.staged")) } == true,
               "the restored item's rolled-back copy is removed, so retained copies are never ambiguous")
    try rejects("a recovery-required transaction keeps blocking new installations") { _ = try broken.install(selection: all) }

    // MARK: Staged copies are never named .vst3 or .component

    let naming = try scenario(existing: [key(2, .vst3)])
    var stagedNames: [String] = []
    naming.afterStagingForTesting = {
        guard let folder = try transactionFolder(naming) else { throw InstallError.message("No transaction folder") }
        stagedNames = try fm.contentsOfDirectory(atPath: folder.path).sorted()
    }
    _ = try naming.install(selection: all)
    try expect(stagedNames == (0..<10).map { "\($0).staged" } + ["transaction.json"],
               "staged copies use extension-less names that audio apps and the AU registrar never scan")

    // MARK: Single-plugin installer leftovers

    let singleLeftover = try scenario(existing: [key(0, .vst3)])
    let stageCopy = singleLeftover.locations.userVST3.appendingPathComponent(".yoi-boi-stage-\(UUID().uuidString).vst3")
    try bundle(at: stageCopy, key(0, .vst3), version: "0.0.9")
    let singleState = try state(singleLeftover)
    try rejectsWith("a single-plugin installer's temporary copy is explained, not called a renamed copy", containing: "interrupted single-plugin update") {
        _ = try singleLeftover.install(selection: all)
    }
    try expect(exists(stageCopy) && (try state(singleLeftover)) == singleState, "an unproven temporary copy is never deleted")
    try fm.removeItem(at: stageCopy)
    try fm.copyItem(at: singleLeftover.destinationURL(key(0, .vst3)), to: stageCopy)
    _ = try singleLeftover.install(selection: all)
    try expect(!exists(stageCopy) && (try state(singleLeftover)) == payloadHashes, "a temporary copy identical to the installed plug-in is removed and the suite installs")
    let auStage = try scenario()
    try fm.createDirectory(at: auStage.locations.userComponents, withIntermediateDirectories: true)
    let auStageCopy = auStage.locations.userComponents.appendingPathComponent(".throwup-stage-\(UUID().uuidString).component")
    try bundle(at: auStageCopy, key(1, .au), version: "0.0.9")
    try rejectsWith("a single-plugin AU temporary copy in Components is explained", containing: "interrupted single-plugin update") {
        _ = try auStage.install(selection: auItems)
    }

    // MARK: Duplicates by identifier, class ID or AU codes, recursively; system folders are only read

    let nested = try scenario(existing: [key(3, .vst3)])
    let nestedCopy = nested.locations.userVST3.appendingPathComponent("Old Versions/SHIFTY.vst3")
    try fm.createDirectory(at: nestedCopy.deletingLastPathComponent(), withIntermediateDirectories: true)
    try fm.copyItem(at: oldRoot.appendingPathComponent("SHIFTY.vst3"), to: nestedCopy)
    let nestedState = try state(nested)
    try rejectsWith("a duplicate in a subfolder of the VST3 folder is found", containing: "Old Versions/SHIFTY.vst3") { _ = try nested.install(selection: all) }
    try expect(try state(nested) == nestedState, "the subfolder duplicate refusal changes nothing")
    let clone = try scenario()
    let cloneURL = clone.locations.userVST3.appendingPathComponent("Clone.vst3")
    try bundle(at: cloneURL, key(1, .vst3), version: "0.0.1")
    try setInfo("com.someone.clone", for: "CFBundleIdentifier", in: cloneURL)
    try rejects("a copy with a suite plug-in's VST3 class IDs under another bundle identifier is a duplicate") { _ = try clone.install(selection: all) }
    let thirdParty = try scenario()
    let thirdPartyBundle = thirdParty.locations.systemVST3.appendingPathComponent("SHIFTY.vst3")
    try fm.createDirectory(at: thirdPartyBundle.appendingPathComponent("Contents"), withIntermediateDirectories: true)
    try PropertyListSerialization.data(fromPropertyList: ["CFBundleIdentifier": "com.othervendor.shifty"], format: .xml, options: 0)
        .write(to: thirdPartyBundle.appendingPathComponent("Contents/Info.plist"))
    try expect((try thirdParty.install(selection: all)).installed.count == 10, "another vendor's system plug-in that shares a name does not block the suite")
    let relocated = try scenario()
    let realSystem = root.appendingPathComponent("RelocatedSystemComponents")
    try fm.createDirectory(at: realSystem, withIntermediateDirectories: true)
    try fm.createDirectory(at: relocated.locations.systemComponents.deletingLastPathComponent(), withIntermediateDirectories: true)
    try fm.createSymbolicLink(at: relocated.locations.systemComponents, withDestinationURL: realSystem)
    try expect((try relocated.install(selection: all)).installed.count == 10, "a relocated (symlinked) system Components folder is read, not refused")
    try fm.copyItem(at: oldRoot.appendingPathComponent("SHIFTY.component"), to: realSystem.appendingPathComponent("SHIFTY.component"))
    try rejects("a duplicate inside a symlinked system Components folder is still detected") { _ = try relocated.install(selection: auItems) }

    // MARK: Lock and permission errors

    let held = try scenario()
    let otherLock = open(held.locations.userVST3.appendingPathComponent(SuiteInstallerEngine.lockName).path, O_CREAT | O_RDWR, 0o600)
    guard otherLock >= 0, flock(otherLock, LOCK_EX | LOCK_NB) == 0 else { throw InstallError.message("Cannot hold the fixture lock") }
    try rejectsWith("a lock held by another installer gets a plain explanation", containing: "Another Vivid Fever Dreams installer") { _ = try held.install(selection: auItems) }
    flock(otherLock, LOCK_UN); close(otherLock)
    try expect((try held.install(selection: all)).installed.count == 10, "the leftover lock file does not block once nobody holds it")
    let hardLinked = try scenario()
    let lockFile = hardLinked.locations.userVST3.appendingPathComponent(SuiteInstallerEngine.lockName)
    try Data().write(to: lockFile)
    try fm.linkItem(at: lockFile, to: root.appendingPathComponent("lock-hard-link"))
    try rejectsWith("a hard-linked lock file is refused with directions", containing: "not a normal file owned by you") { _ = try hardLinked.install(selection: all) }
    let readOnly = try scenario()
    try fm.createDirectory(at: readOnly.locations.userComponents, withIntermediateDirectories: true)
    _ = chmod(readOnly.locations.userComponents.path, 0o555)
    try rejectsWith("a Components folder the user can't write to gets a plain explanation", containing: "AU folder can’t be changed by your user account") {
        _ = try readOnly.install(selection: auItems)
    }
    _ = chmod(readOnly.locations.userComponents.path, 0o755)

    // MARK: Versions and payload paths

    try expect(VersionOrder.compare("1.0", "1.0.0") == .orderedSame && VersionOrder.compare("0.10.2", "0.9.0") == .orderedDescending
               && VersionOrder.compare("1.2.9", "1.2.10") == .orderedAscending && VersionOrder.compare("2", "10") == .orderedAscending
               && VersionOrder.compare("0.4.2.1", "0.4.2") == .orderedDescending && VersionOrder.compare("007.1", "7.1") == .orderedSame,
               "versions compare numerically by component, padding missing components with zero")
    let padded = try scenario()
    try bundle(at: padded.destinationURL(key(3, .vst3)), key(3, .vst3), version: SuiteCatalog.plugins[3].version + ".0")
    try expect((try padded.install(selection: all)).updated == [key(3, .vst3)], "an installed 0.5.0.0 counts as the same version as 0.5.0, not a newer one")
    let aliasPayload = root.appendingPathComponent("PayloadAlias")
    try fm.createSymbolicLink(at: aliasPayload, withDestinationURL: payloadRoot)
    let viaAlias = try scenario()
    try expect((try makeEngine(viaAlias.locations, payload: aliasPayload).install(selection: all)).installed.count == 10,
               "an installer opened through a symlinked folder still verifies and installs its payload")

    // MARK: Uninstall moves verified bundles to the Trash

    let removal = try scenario()
    _ = try removal.install(selection: all)
    let refreshesBeforeRemoval = refreshCount
    let removing: Set<ItemKey> = [key(0, .vst3), key(0, .au), key(3, .vst3), key(4, .au)]
    let removed = try removal.uninstall(selection: removing)
    try expect(removed.removed.count == 4 && removing.allSatisfy { !exists(removal.destinationURL($0)) } && (try state(removal)).count == 6,
               "uninstall removes exactly the four selected bundles")
    try expect(trash(removal).count == 4 && removed.removed.allSatisfy { item in
        item.trashedAt.map { (try? SuiteInstallerEngine.fileHashes(in: $0)) == manifest.entry(item.key).files } == true
    }, "the removed bundles are intact in the Trash, so uninstall can be undone")
    try expect(refreshCount == refreshesBeforeRemoval + 1 && removed.audioUnitsRefreshed,
               "removing an AU refreshes the Audio Unit list")
    let removedAgain = try removal.uninstall(selection: [key(0, .vst3), key(1, .vst3)])
    try expect(removedAgain.notInstalled == [key(0, .vst3)] && removedAgain.removed.map(\.key) == [key(1, .vst3)],
               "already-removed items are reported as not installed")
    try setInfo("com.someone.else", for: "CFBundleIdentifier", in: removal.destinationURL(key(2, .vst3)))
    let trashCount = trash(removal).count
    try rejectsWith("an unverified bundle stops the whole uninstall", containing: "Nothing was moved to the Trash") {
        _ = try removal.uninstall(selection: [key(2, .vst3), key(1, .au)])
    }
    try expect(exists(removal.destinationURL(key(1, .au))) && trash(removal).count == trashCount, "nothing moves when any selected item fails verification")
    let logicUninstall = try scenario(hosts: { formats in formats.contains(.au) ? ["Logic Pro"] : [] })
    _ = try logicUninstall.install(selection: vst3Items)
    try rejectsWith("removing an AU while Logic Pro runs is refused", containing: "Logic Pro") { _ = try logicUninstall.uninstall(selection: auItems) }

    // MARK: Repair

    // A transaction interrupted after two commits: YOI-BOI VST3 replaced (backup in 0.staged), YOI-BOI AU newly added.
    let crashed = try scenario(existing: [key(0, .vst3)])
    let crashedKeys = [key(0, .vst3), key(0, .au)]
    let crashFolder = crashed.locations.userVST3.appendingPathComponent(SuiteInstallerEngine.transactionPrefix + UUID().uuidString)
    try fm.createDirectory(at: crashFolder, withIntermediateDirectories: false)
    let crashSnapshots = try snapshots(crashed, crashedKeys, withDestinations: true)
    try fm.moveItem(at: crashed.destinationURL(key(0, .vst3)), to: crashFolder.appendingPathComponent("0.staged"))
    try fm.copyItem(at: payloadRoot.appendingPathComponent("YOI-BOI.vst3"), to: crashed.destinationURL(key(0, .vst3)))
    try fm.createDirectory(at: crashed.locations.userComponents, withIntermediateDirectories: true)
    try fm.copyItem(at: payloadRoot.appendingPathComponent("YOI-BOI.component"), to: crashed.destinationURL(key(0, .au)))
    let crashJournal = SuiteJournal(format: 3, state: "committing", destination: crashed.locations.userVST3.path, suiteVersion: "1.3.0",
        previous: crashSnapshots, payload: Dictionary(uniqueKeysWithValues: crashedKeys.map { ($0.filename, manifest.entry($0).files) }),
        items: [SuiteJournalItem(filename: "YOI-BOI.vst3", stage: "0.staged", state: "committed", destination: crashed.destinationURL(key(0, .vst3)).path),
                SuiteJournalItem(filename: "YOI-BOI.component", stage: "1.staged", state: "committed", destination: crashed.destinationURL(key(0, .au)).path)],
        committed: [0, 1])
    try JSONEncoder().encode(crashJournal).write(to: crashFolder.appendingPathComponent("transaction.json"))
    try rejectsWith("an interrupted transaction blocks installation and points to Repair", containing: "Repair") { _ = try crashed.install(selection: all) }
    let crashIssue = crashed.repairIssues().first { $0.url.lastPathComponent == crashFolder.lastPathComponent }
    try expect(crashIssue?.canRestore == true && crashIssue?.kind == .suiteTransaction(state: "committing", safe: false),
               "Repair finds the interrupted transaction and offers to restore it")
    let refreshesBeforeRepair = refreshCount
    if let crashIssue = crashIssue { _ = try crashed.restorePrevious(crashIssue) }
    try expect(try SuiteInstallerEngine.fileHashes(in: crashed.destinationURL(key(0, .vst3))) == crashSnapshots[0].hashes
               && !exists(crashed.destinationURL(key(0, .au))) && !exists(crashFolder),
               "Restore Previous Versions puts back the hash-verified backup and removes the added AU")
    try expect(trash(crashed).count == 2 && refreshCount == refreshesBeforeRepair + 1, "the added AU and the finished folder go to the Trash; AUs are refreshed")
    try expect((try crashed.install(selection: all)).installed.count == 9, "installation works again after the repair")
    let brokenIssue = broken.repairIssues().first { if case .suiteTransaction = $0.kind { return true }; return false }
    try expect(brokenIssue?.canRestore == false && brokenIssue?.detail.contains("changed after the interrupted installation") == true,
               "a transaction whose destination changed afterwards is not restored automatically")
    if let brokenIssue = brokenIssue { _ = try broken.cleanUp(brokenIssue) }
    broken.failAfterCommitForTesting = nil
    broken.afterCommitForTesting = nil
    let afterCleanUp = try broken.install(selection: all)
    try expect(trash(broken).count == 1 && afterCleanUp.installed.count + afterCleanUp.updated.count == 10 && (try clean(broken)),
               "Clean Up moves it to the Trash and installation works again")
    let leftovers = try scenario()
    try fm.createDirectory(at: leftovers.locations.userComponents, withIntermediateDirectories: true)
    let plainStage = leftovers.locations.userVST3.appendingPathComponent(".shifty-stage-\(UUID().uuidString)")
    try bundle(at: plainStage, key(3, .vst3), version: "0.0.9")
    let componentStage = leftovers.locations.userComponents.appendingPathComponent(".throwup-stage-\(UUID().uuidString).component")
    try bundle(at: componentStage, key(1, .au), version: "0.0.9")
    let discarded = leftovers.locations.userVST3.appendingPathComponent(SuiteInstallerEngine.discardPrefix + "LEFT")
    try fm.createDirectory(at: discarded, withIntermediateDirectories: false)
    let safeFolder = try leftover(in: leftovers, state: "staging", format: 3, payload: true, keys: [key(2, .vst3)])
    let foundIssues = leftovers.repairIssues()
    try expect(foundIssues.filter { $0.kind == .singlePluginStage }.count == 2 && foundIssues.contains { $0.kind == .finishedTransaction }
               && foundIssues.contains { $0.url.path == safeFolder.path && $0.kind == .suiteTransaction(state: "staging", safe: true) && !$0.canRestore },
               "Repair lists single-plugin temporary copies, finished leftovers and safe transactions")
    for issue in foundIssues { _ = try leftovers.cleanUp(issue) }
    try expect(leftovers.repairIssues().isEmpty && trash(leftovers).count == 4, "Clean Up moves every leftover to the Trash")
    let saved = try scenario()
    _ = try saved.install(selection: all)
    try fm.createDirectory(at: saved.locations.recovered, withIntermediateDirectories: true)
    let savedCopy = saved.locations.recovered.appendingPathComponent("THROW-UP 0.0.1 (saved 2026-10-05 12.00.00).vst3")
    try fm.copyItem(at: oldRoot.appendingPathComponent("THROW-UP.vst3"), to: savedCopy)
    let savedHashes = try SuiteInstallerEngine.fileHashes(in: savedCopy)
    try fm.copyItem(at: oldRoot.appendingPathComponent("THROW-UP.vst3"), to: saved.locations.recovered.appendingPathComponent("Unrelated.vst3"))
    try setInfo("com.someone.else", for: "CFBundleIdentifier", in: saved.locations.recovered.appendingPathComponent("Unrelated.vst3"))
    let savedIssues = saved.repairIssues()
    try expect(savedIssues.count == 1 && savedIssues[0].kind == .recoveredCopy(key(1, .vst3)) && savedIssues[0].canRestore,
               "Repair offers a verified copy from the Recovered folder and ignores unrelated items")
    _ = try saved.restorePrevious(savedIssues[0])
    try expect(try SuiteInstallerEngine.fileHashes(in: saved.destinationURL(key(1, .vst3))) == savedHashes && !exists(savedCopy) && trash(saved).count == 1,
               "restoring a saved copy puts it in place and moves the current copy to the Trash")

    // MARK: What's new, rescan guidance and the result summary

    let names = SuiteCatalog.plugins.map(\.name)
    let notes = Dictionary(uniqueKeysWithValues: names.map { ($0, ["Now also an Audio Unit."]) })
    let notesData = try JSONSerialization.data(withJSONObject: notes)
    try expect(try WhatsNew.parse(notesData, requiredNames: names, allowOtherNames: false) == notes, "valid What’s new notes parse")
    try rejects("What’s new without every plug-in is refused") {
        _ = try WhatsNew.parse(try JSONSerialization.data(withJSONObject: ["YOI-BOI": ["x"]]), requiredNames: names, allowOtherNames: false)
    }
    var unknown = notes; unknown["MYSTERY"] = ["x"]
    try rejects("What’s new naming an unknown plug-in is refused") {
        _ = try WhatsNew.parse(try JSONSerialization.data(withJSONObject: unknown), requiredNames: names, allowOtherNames: false)
    }
    var tooLong = notes; tooLong["SHIFTY"] = Array(repeating: "line", count: 7)
    try rejects("What’s new with too many lines is refused") {
        _ = try WhatsNew.parse(try JSONSerialization.data(withJSONObject: tooLong), requiredNames: names, allowOtherNames: false)
    }
    try expect((try WhatsNew.parse(notesData, requiredNames: ["SHIFTY"], allowOtherNames: true))["SHIFTY"] == ["Now also an Audio Unit."],
               "a single-plugin installer can read its own entry from the suite file")
    let both = RescanGuidance.steps(installedBundleIdentifiers: ["com.ableton.live", "com.apple.logic10"], formats: [.vst3, .au])
    try expect(both.count == 2 && both[0].contains("Option-click Rescan") && both[0].contains("Use Audio Units v2") && both[1].contains("Reset & Rescan Selection"),
               "guidance covers Ableton Live (full rescan) and Logic Pro (Plug-in Manager)")
    let vstOnlyGuidance = RescanGuidance.steps(installedBundleIdentifiers: ["com.apple.logic10", "com.bitwig.BitwigStudio", "com.cockos.reaper", "com.image-line.flstudio"],
                                               formats: [.vst3])
    try expect(vstOnlyGuidance.count == 4 && vstOnlyGuidance.contains { $0.contains("restart Bitwig") } && vstOnlyGuidance.contains { $0.contains("Tick AU") }
               && vstOnlyGuidance.contains { $0.contains("Re-scan") } && vstOnlyGuidance.contains { $0.contains("Find installed plugins") },
               "VST3-only guidance tells Logic users to tick AU and covers Bitwig, REAPER and FL Studio")
    try expect(RescanGuidance.steps(installedBundleIdentifiers: [], formats: [.au]) == ["Open your audio app and rescan its Audio Units."],
               "without a known DAW the guidance stays generic")
    let summary = ResultSummary.installSections(InstallResult(installed: [key(0, .au)], updated: [key(2, .vst3)], skipped: [key(3, .vst3)],
        audioUnitsRefreshed: true, log: [], warnings: []), hostIdentifiers: ["com.apple.logic10"], whatsNew: notes)
    let lines = summary.flatMap(\.lines)
    try expect(summary.map(\.heading) == ["Done", "Next steps", "What’s new"] && lines.contains { $0.hasPrefix("YOI-BOI 0.2.0:") }
               && lines.contains { $0.hasPrefix("BACK-SCRATCH 0.11.0:") } && !lines.contains { $0.hasPrefix("SHIFTY") && $0.contains("Now also") }
               && lines.contains { $0.contains("Restart Logic Pro") }, "the result page shows guidance and What’s new for the plug-ins that changed")

    // MARK: Plain-English errors and the appended log

    let outOfSpace = NSError(domain: NSCocoaErrorDomain, code: NSFileWriteOutOfSpaceError,
                             userInfo: [NSUnderlyingErrorKey: NSError(domain: NSPOSIXErrorDomain, code: Int(ENOSPC))])
    try expect(UserFacingText.message(for: outOfSpace).contains("disk space") && UserFacingText.logText(for: outOfSpace).contains("Details:"),
               "a full disk is explained in plain English while the raw error stays in the log")
    try expect(UserFacingText.message(for: NSError(domain: NSPOSIXErrorDomain, code: Int(EACCES))).contains("did not allow"),
               "a permission error is explained in plain English")
    let technical = InstallError.technical("Friendly summary.", detail: "codesign raw output")
    try expect(UserFacingText.message(for: technical) == "Friendly summary." && UserFacingText.logText(for: technical).contains("codesign raw output"),
               "technical details reach the log, not the window")
    let logFile = root.appendingPathComponent("Logs/installer.log")
    try InstallLog.append(["first run"], to: logFile, limit: 40)
    try InstallLog.append(["second run"], to: logFile, limit: 40)
    let bothRuns = try String(contentsOf: logFile)
    try expect(bothRuns.contains("first run") && bothRuns.contains("second run"), "the log keeps earlier attempts instead of overwriting them")
    try InstallLog.append(["third run"], to: logFile, limit: 40)
    let rotatedLog = try String(contentsOf: logFile.appendingPathExtension("1"))
    let currentLog = try String(contentsOf: logFile)
    try expect(rotatedLog.contains("second run") && currentLog.contains("third run") && !currentLog.contains("first run"),
               "a log past its size limit rotates to installer.log.1")
    print("Suite installer self-test passed: \(checks) checks. All fixture writes stayed inside \(root.path).")
}
