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
    func setBundleIdentifier(_ identifier: String, in bundle: URL) throws {
        let plist = bundle.appendingPathComponent("Contents/Info.plist")
        var object = try PropertyListSerialization.propertyList(from: Data(contentsOf: plist), format: nil) as? [String: Any] ?? [:]
        object["CFBundleIdentifier"] = identifier
        try PropertyListSerialization.data(fromPropertyList: object, format: .xml, options: 0).write(to: plist)
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
    let payloadHashes = Dictionary(uniqueKeysWithValues: entries.map { ($0.filename, $0.files) })
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
            if exists(file) { result[definition.filename] = try SuiteInstallerEngine.fileHashes(in: file) }
        }
        return result
    }
    func clean(_ engine: SuiteInstallerEngine) throws -> Bool {
        try !fm.contentsOfDirectory(atPath: engine.destinationRoot.path).contains {
            $0.hasPrefix(SuiteInstallerEngine.transactionPrefix) || $0.hasPrefix(SuiteInstallerEngine.discardPrefix)
        }
    }
    func transactionFolder(_ engine: SuiteInstallerEngine) throws -> URL? {
        try fm.contentsOfDirectory(at: engine.destinationRoot, includingPropertiesForKeys: nil)
            .first { $0.lastPathComponent.hasPrefix(SuiteInstallerEngine.transactionPrefix) }
    }
    func isSubsequence(_ wanted: [String], of states: [String]) -> Bool {
        var remaining = wanted[...]
        for state in states where state == remaining.first { remaining = remaining.dropFirst() }
        return remaining.isEmpty
    }

    // MARK: Complete-suite transaction (existing coverage)

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
    try expect(install.warnings.isEmpty && reinstall.warnings.isEmpty, "a normal installation reports no warnings")
    let mixed = try scenario(existing: [0, 2])
    let mixedResult = try mixed.install()
    try expect(mixedResult.installed == 3 && mixedResult.updated == 2, "mixed new/update suite commits all five")
    // Every possible partial commit is exercised for new, update, and mixed suites.
    for existing: Set<Int> in [[], [0, 1, 2, 3, 4], [0, 2]] {
        for commit in 1...SuiteCatalog.plugins.count {
            let engine = try scenario(existing: existing)
            let before = try state(engine)
            var observed: [SuiteJournal] = []
            engine.journalObserverForTesting = { observed.append($0) }
            engine.failAfterCommitForTesting = commit
            try rejects("fault after commit \(commit), \(existing.count) existing bundles, rejects installation") { _ = try engine.install() }
            try expect(try state(engine) == before, "fault \(commit), \(existing.count) existing bundles, restores complete prior suite")
            try expect(try clean(engine), "fault \(commit), \(existing.count) existing bundles, cleans transaction files")
            let items = observed.last?.items ?? []
            try expect(observed.last?.state == "rolled-back" && items.filter { $0.state == "restored" }.count == commit &&
                       items.filter { $0.state == "pending" }.count == SuiteCatalog.plugins.count - commit,
                       "fault \(commit), \(existing.count) existing bundles, journal records restored and pending items")
            let firstItemStates = observed.compactMap { $0.items?.first?.state }
            try expect(isSubsequence(["pending", "committing", "committed", "restoring", "restored"], of: firstItemStates),
                       "fault \(commit), \(existing.count) existing bundles, journal is written before each rename and rollback step")
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
    try setBundleIdentifier("com.unrelated.plugin", in: unrelated.destinationRoot.appendingPathComponent("THROW-UP.vst3"))
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
    try Data("unknown recovery data".utf8).write(to: stale.appendingPathComponent("notes.txt"))
    try rejectsWith("unfinished transaction without a journal blocks another installation", containing: "Command-Shift-Period") { _ = try interrupted.install() }
    try expect(exists(stale.appendingPathComponent("notes.txt")), "unfinished recovery data is retained")
    let stagedTamper = try scenario(existing: [1])
    let stagedState = try state(stagedTamper)
    stagedTamper.afterStagingForTesting = {
        guard let directory = try transactionFolder(stagedTamper) else { throw InstallError.message("No transaction folder") }
        try Data("unexpected staged change".utf8).write(to: directory.appendingPathComponent("3.staged/Contents/Resources/changed.txt"))
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

    // MARK: S1 open-file host detection runs lsof for real

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
    let holderPID = holder.processIdentifier
    let excludedHolder = InstallerHostDetection.openFileUsers(paths: [heldFile.path, unopenedFile.path], excluding: [holderPID])
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

    // MARK: S2 quarantine is never copied into the VST3 folder

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
    let quarantineTarget = try scenario(existing: [1])
    let quarantineEngine = SuiteInstallerEngine(payloadRoot: quarantinedPayload, manifest: manifest, destinationRoot: quarantineTarget.destinationRoot,
        systemRoot: quarantineTarget.systemRoot, runningHosts: { [] })
    let quarantineResult = try quarantineEngine.install()
    let installedPaths = SuiteCatalog.plugins.flatMap { allPaths(quarantineTarget.destinationRoot.appendingPathComponent($0.filename)) }
    try expect(quarantineResult.installed == 4 && allPaths(quarantinedPayload).allSatisfy(hasQuarantine),
               "a fully quarantined payload installs, and the payload itself keeps its flags")
    try expect(installedPaths.count > 10 && !installedPaths.contains(where: hasQuarantine), "installed plug-ins never inherit the download quarantine flag")

    // MARK: S3 launch arguments

    try expect(LaunchMode.parse(["app"]) == .gui, "no arguments opens the window")
    try expect(LaunchMode.parse(["app", "-psn_0_1234567"]) == .gui, "a LaunchServices -psn_ argument still opens the window")
    try expect(LaunchMode.parse(["app", "-NSDocumentRevisionsDebugMode", "YES"]) == .gui, "Apple runtime arguments still open the window")
    try expect(LaunchMode.parse(["app", "-psn_0_1", "--self-test"]) == .selfTest, "known options still work beside a -psn_ argument")
    try expect(LaunchMode.parse(["app", "--make-manifest", "a", "b", "1.2.3"]) == .makeManifest(payload: "a", manifest: "b", version: "1.2.3")
               && LaunchMode.parse(["app", "--check-hosts"]) == .checkHosts, "build and diagnostic options parse")
    try expect(LaunchMode.parse(["app", "--bogus"]) == .invalid && LaunchMode.parse(["app", "--self-test", "extra"]) == .invalid,
               "unknown or malformed -- options are still rejected")

    // MARK: S4 interrupted transactions are cleaned up only when provably safe

    func snapshots(_ engine: SuiteInstallerEngine) throws -> [SuiteSnapshot] {
        try SuiteCatalog.plugins.map { definition in
            let url = engine.destinationRoot.appendingPathComponent(definition.filename)
            guard exists(url) else { return SuiteSnapshot(filename: definition.filename, identity: nil, hashes: nil) }
            return SuiteSnapshot(filename: definition.filename, identity: try SuiteInstallerEngine.identity(at: url, definition: definition),
                                 hashes: try SuiteInstallerEngine.fileHashes(in: url))
        }
    }
    func leftover(in engine: SuiteInstallerEngine, state: String, payload: [String: [String: String]]?) throws -> URL {
        let folder = engine.destinationRoot.appendingPathComponent(SuiteInstallerEngine.transactionPrefix + UUID().uuidString)
        try fm.createDirectory(at: folder, withIntermediateDirectories: false)
        // payload == nil writes the format-1 journal used by suite 1.2.2 and earlier.
        let journal = SuiteJournal(format: payload == nil ? nil : 2, state: state, destination: engine.destinationRoot.path, suiteVersion: nil,
                                   previous: try snapshots(engine), payload: payload, items: nil, committed: [])
        try JSONEncoder().encode(journal).write(to: folder.appendingPathComponent("transaction.json"))
        try fm.copyItem(at: payloadRoot.appendingPathComponent(SuiteCatalog.plugins[0].filename), to: folder.appendingPathComponent("0.staged"))
        return folder
    }
    let safeStaging = try scenario(existing: [0, 2])
    let stagingFolder = try leftover(in: safeStaging, state: "staging", payload: payloadHashes)
    let afterCleanup = try safeStaging.install()
    try expect(!exists(stagingFolder) && (try clean(safeStaging)), "an interrupted staging transaction that changed nothing is cleaned up automatically")
    try expect(afterCleanup.installed == 3 && afterCleanup.updated == 2, "installation proceeds after the automatic cleanup")
    let legacyPrepared = try scenario(existing: [1])
    let legacyFolder = try leftover(in: legacyPrepared, state: "prepared", payload: nil)
    _ = try legacyPrepared.install()
    try expect(!exists(legacyFolder), "a suite 1.2.2 journal left in the prepared state is recognised and cleaned up")
    let changedSince = try scenario(existing: [0])
    let changedFolder = try leftover(in: changedSince, state: "prepared", payload: payloadHashes)
    try Data("changed after the crash".utf8).write(to: changedSince.destinationRoot.appendingPathComponent("YOI-BOI.vst3/Contents/Resources/later.txt"))
    let changedState = try state(changedSince)
    try rejectsWith("an interrupted transaction whose destination changed keeps blocking and names its folder", containing: changedFolder.lastPathComponent) {
        _ = try changedSince.install()
    }
    try expect(exists(changedFolder.appendingPathComponent("0.staged")) && (try state(changedSince)) == changedState,
               "the blocking transaction and the installed plug-ins are left untouched")
    let finished = try scenario()
    _ = try finished.install()
    let finishedFolder = try leftover(in: finished, state: "all-verified", payload: payloadHashes)
    _ = try finished.install()
    try expect(!exists(finishedFolder), "a transaction that had already verified all five is cleaned up")
    let legacyFinished = try scenario()
    _ = try legacyFinished.install()
    let legacyFinishedFolder = try leftover(in: legacyFinished, state: "all-verified", payload: nil)
    try rejects("an all-verified journal without payload hashes is not assumed safe") { _ = try legacyFinished.install() }
    try expect(exists(legacyFinishedFolder), "that unproven transaction is retained")
    let committingLeftover = try scenario(existing: [3])
    let committingFolder = try leftover(in: committingLeftover, state: "committing", payload: payloadHashes)
    try rejects("a transaction interrupted while committing keeps blocking") { _ = try committingLeftover.install() }
    try expect(exists(committingFolder), "the committing transaction is retained")
    let emptyLeftover = try scenario()
    let emptyFolder = emptyLeftover.destinationRoot.appendingPathComponent(SuiteInstallerEngine.transactionPrefix + "EMPTY")
    try fm.createDirectory(at: emptyFolder, withIntermediateDirectories: false)
    let discardLeftover = emptyLeftover.destinationRoot.appendingPathComponent(SuiteInstallerEngine.discardPrefix + "OLD")
    try fm.createDirectory(at: discardLeftover.appendingPathComponent("0.staged"), withIntermediateDirectories: true)
    _ = try emptyLeftover.install()
    try expect(!exists(emptyFolder) && !exists(discardLeftover) && (try clean(emptyLeftover)),
               "empty transaction folders and finished discard folders are removed")
    let stuck = try scenario(existing: [3])
    stuck.afterStagingForTesting = {
        guard let folder = try transactionFolder(stuck) else { throw InstallError.message("No transaction folder") }
        let file = folder.appendingPathComponent("locked.txt")
        try Data("immutable".utf8).write(to: file)
        guard chflags(file.path, 0x2) == 0 else { throw InstallError.message("Cannot set the user-immutable flag") } // UF_IMMUTABLE
    }
    let stuckResult = try stuck.install()
    try expect(stuckResult.warnings.count == 1 && stuckResult.warnings[0].contains(SuiteInstallerEngine.discardPrefix),
               "a temporary folder that cannot be deleted is reported as a warning instead of a silent success")
    try expect(try state(stuck) == payloadHashes, "the installation itself still completed")
    makeRemovable(stuck.destinationRoot)
    stuck.afterStagingForTesting = nil
    _ = try stuck.install()
    try expect(try clean(stuck), "the next installation removes the leftover temporary folder")

    // MARK: S5 the retained journal says exactly which item needs recovery

    let broken = try scenario(existing: [0, 2])
    broken.failAfterCommitForTesting = 2
    broken.afterCommitForTesting = { count in
        if count == 2 {
            try Data("edited by another program".utf8).write(to: broken.destinationRoot.appendingPathComponent("YOI-BOI.vst3/Contents/Resources/outside.txt"))
        }
    }
    try rejectsWith("a rollback that cannot safely restore one plug-in reports recovery", containing: "could not fully restore") { _ = try broken.install() }
    let retained = try transactionFolder(broken)
    let recorded = try retained.map { try JSONDecoder().decode(SuiteJournal.self, from: Data(contentsOf: $0.appendingPathComponent("transaction.json"))) }
    let recordedStates = recorded?.items?.map(\.state) ?? []
    try expect(recorded?.state == "recovery-required" && recordedStates == ["rollback-failed", "restored", "pending", "pending", "pending"],
               "the retained journal records which plug-in failed to restore and which were restored")
    try expect(retained.map { exists($0.appendingPathComponent("0.staged")) && !exists($0.appendingPathComponent("1.staged")) } == true,
               "the restored item's rolled-back copy is removed, so retained copies are never ambiguous")
    try rejects("a recovery-required transaction keeps blocking new installations") { _ = try broken.install() }

    // MARK: S6 staged copies are never named .vst3

    let naming = try scenario(existing: [2])
    var stagedNames: [String] = []
    naming.afterStagingForTesting = {
        guard let folder = try transactionFolder(naming) else { throw InstallError.message("No transaction folder") }
        stagedNames = try fm.contentsOfDirectory(atPath: folder.path).sorted()
    }
    _ = try naming.install()
    try expect(stagedNames == ["0.staged", "1.staged", "2.staged", "3.staged", "4.staged", "transaction.json"],
               "staged copies use extension-less names that audio apps never scan as plug-ins")

    // MARK: S7 single-plugin installer leftovers

    let singleLeftover = try scenario(existing: [0])
    let stageCopy = singleLeftover.destinationRoot.appendingPathComponent(".yoi-boi-stage-\(UUID().uuidString).vst3")
    try bundle(at: stageCopy, index: 0, version: "0.0.9")
    let singleState = try state(singleLeftover)
    try rejectsWith("a single-plugin installer's temporary copy is explained, not called a renamed copy",
                    containing: "interrupted single-plugin update") { _ = try singleLeftover.install() }
    try expect(exists(stageCopy) && (try state(singleLeftover)) == singleState, "an unproven temporary copy is never deleted")
    try fm.removeItem(at: stageCopy)
    try fm.copyItem(at: singleLeftover.destinationRoot.appendingPathComponent("YOI-BOI.vst3"), to: stageCopy)
    _ = try singleLeftover.install()
    try expect(!exists(stageCopy) && (try state(singleLeftover)) == payloadHashes,
               "a temporary copy identical to the installed plug-in is removed and the suite installs")

    // MARK: S8 host list

    func host(_ identifier: String?, _ name: String?) -> Bool {
        InstallerHostDetection.isAudioApplication(bundleIdentifier: identifier, name: name, processID: 10, installerProcessID: 11)
    }
    try expect(!host("com.apple.logic10", "Logic Pro") && !host("com.apple.garageband10", "GarageBand") && !host("com.avid.ProTools", "Pro Tools"),
               "Audio Unit- and AAX-only hosts no longer block a VST3 installation")
    try expect(!host("com.steinberg.activationmanager", "Steinberg Activation Manager") && !host("com.presonus.universalcontrol", "Universal Control")
               && !host("com.motu.cuemix5", "CueMix 5") && !host("com.image-line.flcloud", "FL Cloud Plugins"),
               "vendor utilities are not mistaken for audio hosts")
    try expect(host("com.steinberg.cubase13", nil) && host("com.steinberg.nuendo13", nil) && host("com.presonus.studioone6", nil)
               && host("com.bitwig.BitwigStudio", nil) && host("com.cockos.reaper", nil) && host("com.image-line.flstudio", nil)
               && host("com.Tracktion.pluginval", nil) && host("com.cycling74.Max", nil) && host("com.motu.DigitalPerformer", nil),
               "VST3 hosts are detected by bundle identifier")
    try expect(host(nil, "Gig Performer 5") && host(nil, "Maschine 2") && host(nil, "Vienna Ensemble Pro 7") && host(nil, "Renoise")
               && host(nil, "Komplete Kontrol") && host(nil, "Reason 12"), "VST3 hosts without a listed identifier are detected by name")
    try expect(!host(nil, "Element") && !host(nil, "Reason Studios Companion") && !host(nil, "Maxwell"),
               "name matching does not catch unrelated apps")

    // MARK: S9 duplicates by identifier and class ID, recursively; the system folder is only read

    let nested = try scenario(existing: [3])
    let nestedCopy = nested.destinationRoot.appendingPathComponent("Old Versions/SHIFTY.vst3")
    try fm.createDirectory(at: nestedCopy.deletingLastPathComponent(), withIntermediateDirectories: true)
    try fm.copyItem(at: oldRoot.appendingPathComponent("SHIFTY.vst3"), to: nestedCopy)
    let nestedState = try state(nested)
    try rejectsWith("a duplicate in a subfolder of the VST3 folder is found", containing: "Old Versions/SHIFTY.vst3") { _ = try nested.install() }
    try expect(try state(nested) == nestedState, "the subfolder duplicate refusal changes nothing")
    let clone = try scenario()
    let cloneURL = clone.destinationRoot.appendingPathComponent("Clone.vst3")
    try bundle(at: cloneURL, index: 1, version: "0.0.1")
    try setBundleIdentifier("com.someone.clone", in: cloneURL)
    try rejects("a copy with a suite plug-in's VST3 class IDs under another bundle identifier is a duplicate") { _ = try clone.install() }
    let thirdParty = try scenario()
    let thirdPartyBundle = thirdParty.systemRoot.appendingPathComponent("SHIFTY.vst3")
    try fm.createDirectory(at: thirdPartyBundle.appendingPathComponent("Contents"), withIntermediateDirectories: true)
    try PropertyListSerialization.data(fromPropertyList: ["CFBundleIdentifier": "com.othervendor.shifty"], format: .xml, options: 0)
        .write(to: thirdPartyBundle.appendingPathComponent("Contents/Info.plist"))
    try expect((try thirdParty.install()).installed == 5, "another vendor's system plug-in that shares a name does not block the suite")
    let relocated = try scenario()
    let realSystem = root.appendingPathComponent("RelocatedSystemVST3")
    try fm.createDirectory(at: realSystem, withIntermediateDirectories: true)
    try fm.createDirectory(at: relocated.systemRoot.deletingLastPathComponent(), withIntermediateDirectories: true)
    try fm.createSymbolicLink(at: relocated.systemRoot, withDestinationURL: realSystem)
    try expect((try relocated.install()).installed == 5, "a relocated (symlinked) system VST3 folder is read, not refused")
    try fm.copyItem(at: oldRoot.appendingPathComponent("SHIFTY.vst3"), to: realSystem.appendingPathComponent("SHIFTY.vst3"))
    try rejects("a duplicate inside a symlinked system VST3 folder is still detected") { _ = try relocated.install() }

    // MARK: S10 lock and permission errors

    let held = try scenario()
    let otherLock = open(held.destinationRoot.appendingPathComponent(SuiteInstallerEngine.lockName).path, O_CREAT | O_RDWR, 0o600)
    guard otherLock >= 0, flock(otherLock, LOCK_EX | LOCK_NB) == 0 else { throw InstallError.message("Cannot hold the fixture lock") }
    try rejectsWith("a lock held by another installer gets a plain explanation", containing: "Another Vivid Fever Dreams installer") { _ = try held.install() }
    flock(otherLock, LOCK_UN); close(otherLock)
    try expect((try held.install()).installed == 5, "the leftover lock file does not block once nobody holds it")
    let hardLinked = try scenario()
    let lockFile = hardLinked.destinationRoot.appendingPathComponent(SuiteInstallerEngine.lockName)
    try Data().write(to: lockFile)
    try fm.linkItem(at: lockFile, to: root.appendingPathComponent("lock-hard-link"))
    try rejectsWith("a hard-linked lock file is refused with directions", containing: "not a normal file owned by you") { _ = try hardLinked.install() }
    let readOnly = try scenario()
    _ = chmod(readOnly.destinationRoot.path, 0o555)
    try rejectsWith("a VST3 folder the user can't write to gets a plain explanation", containing: "can’t be changed by your user account") {
        _ = try readOnly.install()
    }
    _ = chmod(readOnly.destinationRoot.path, 0o755)

    // MARK: S11 numeric versions

    try expect(VersionOrder.compare("1.0", "1.0.0") == .orderedSame && VersionOrder.compare("0.10.2", "0.9.0") == .orderedDescending
               && VersionOrder.compare("1.2.9", "1.2.10") == .orderedAscending && VersionOrder.compare("2", "10") == .orderedAscending
               && VersionOrder.compare("0.4.2.1", "0.4.2") == .orderedDescending && VersionOrder.compare("007.1", "7.1") == .orderedSame,
               "versions compare numerically by component, padding missing components with zero")
    let padded = try scenario()
    try bundle(at: padded.destinationRoot.appendingPathComponent("SHIFTY.vst3"), index: 3, version: SuiteCatalog.plugins[3].version + ".0")
    try expect((try padded.install()).updated == 1, "an installed 0.4.2.0 counts as the same version as 0.4.2, not a newer one")

    // MARK: S12 installer identifiers

    try expect(InstallerHostDetection.isInstaller("com.yoiboi.energyharvester.installer") && InstallerHostDetection.isInstaller("COM.YOIBOI.SHIFTY.INSTALLER")
               && InstallerHostDetection.isInstaller("com.yoiboi.installer") && !InstallerHostDetection.isInstaller("com.yoiboi.synth"),
               "every Vivid Fever Dreams installer, including ENERGY HARVESTER's, is recognised")

    // MARK: S13 the payload may be reached through a symlink

    let aliasPayload = root.appendingPathComponent("PayloadAlias")
    try fm.createSymbolicLink(at: aliasPayload, withDestinationURL: payloadRoot)
    let viaAlias = try scenario()
    let aliasEngine = SuiteInstallerEngine(payloadRoot: aliasPayload, manifest: manifest, destinationRoot: viaAlias.destinationRoot,
                                           systemRoot: viaAlias.systemRoot, runningHosts: { [] })
    try expect((try aliasEngine.install()).installed == 5, "an installer opened through a symlinked folder still verifies and installs its payload")

    // MARK: S14 plain-English errors and the appended log

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
