import Foundation
import Darwin

func runInstallerTests() throws {
    let fm = FileManager.default
    guard let canonicalTemp = realpath(fm.temporaryDirectory.path, nil) else {
        throw InstallError.message("Cannot resolve the temporary test directory.")
    }
    let temporaryRoot = String(cString: canonicalTemp)
    free(canonicalTemp)
    let root = URL(fileURLWithPath: temporaryRoot).appendingPathComponent("throwup-installer-tests-\(UUID().uuidString)")
    try fm.createDirectory(at: root, withIntermediateDirectories: true)
    defer { try? fm.removeItem(at: root) }
    var count = 0
    let componentID = "0123456789ABCDEF0123456789ABCDEF"
    let controllerID = "FEDCBA9876543210FEDCBA9876543210"
    func expect(_ truth: @autoclosure () throws -> Bool, _ message: String) throws {
        guard try truth() else { throw InstallError.message("SELF-TEST FAILED: \(message)") }
        count += 1
        print("PASS: \(message)")
    }
    func rejects(_ description: String, _ action: () throws -> Void) throws {
        var failed = false
        do { try action() } catch { failed = true }
        try expect(failed, description)
    }
    func rejectsWith(_ description: String, containing text: String, _ action: () throws -> Void) throws {
        var message: String?
        do { try action() } catch { message = error.localizedDescription }
        try expect(message?.contains(text) == true, description)
    }
    let installerPID: Int32 = 100
    let otherInstallerPID: Int32 = 101
    try expect(!InstallerHostDetection.isAudioApplication(bundleIdentifier: "com.yoiboi.throwup.installer",
        name: "THROW-UP-Installer", processID: otherInstallerPID, installerProcessID: installerPID),
        "a second THROW-UP installer is not mistaken for an audio host")
    try expect(!InstallerHostDetection.isAudioApplication(bundleIdentifier: "COM.YOIBOI.THROWUP.INSTALLER",
        name: "Install THROW-UP", processID: otherInstallerPID, installerProcessID: installerPID),
        "installer exclusion accepts case-insensitive bundle identifiers")
    try expect(!InstallerHostDetection.isAudioApplication(bundleIdentifier: "com.ableton.live",
        name: "Ableton Live", processID: installerPID, installerProcessID: installerPID),
        "the current installer process is excluded before application matching")
    try expect(InstallerHostDetection.isAudioApplication(bundleIdentifier: "com.ableton.live",
        name: nil, processID: 102, installerProcessID: installerPID),
        "Ableton is detected by bundle identifier without an application name")
    try expect(InstallerHostDetection.isAudioApplication(bundleIdentifier: nil,
        name: "REAPER", processID: 102, installerProcessID: installerPID),
        "audio hosts without a bundle identifier are detected by name")
    try expect(InstallerHostDetection.isAudioApplication(bundleIdentifier: nil,
        name: "pluginval", processID: 102, installerProcessID: installerPID),
        "the plugin validation host is still detected")
    try expect(!InstallerHostDetection.isAudioApplication(bundleIdentifier: "com.yoiboi.throwup.utility",
        name: "THROW-UP Utility", processID: 102, installerProcessID: installerPID),
        "sharing THROW-UP's bundle prefix does not make an app an audio host")
    try expect(!InstallerHostDetection.isAudioApplication(bundleIdentifier: nil,
        name: nil, processID: 102, installerProcessID: installerPID),
        "an application with no identifying metadata is not falsely blocked")
    let openFileOutput = """
    cUnassociated command
    p100
    cTHROW-UP-Installer
    p101
    cTHROW-UP-Installer
    p102
    cUnknown Audio Host
    p103
    cAbleton Live
    pnot-a-pid
    cInvalid process
    """
    try expect(InstallerHostDetection.fileUsers(openFileOutput, excluding: [installerPID, otherInstallerPID])
        == ["Unknown Audio Host", "Ableton Live"],
        "open-file detection excludes both installers and retains real known and unknown hosts")
    try expect(InstallerHostDetection.fileUsers("", excluding: [installerPID]).isEmpty,
        "no open-file records means no running host")
    let otherInstallerHosts: (Set<PluginFormat>) -> [String] = { _ in
        InstallerHostDetection.isAudioApplication(bundleIdentifier: "com.yoiboi.throwup.installer",
            name: "THROW-UP-Installer", processID: otherInstallerPID, installerProcessID: installerPID)
            ? ["THROW-UP-Installer"] : []
    }
    /// A fixture bundle: VST3 with moduleinfo class IDs, AU with AudioComponents entries.
    func bundle(_ name: String, version: String, format: PluginFormat = .vst3, audioUnit: AudioUnitIdentity = InstallerEngine.audioUnit) throws -> URL {
        let result = root.appendingPathComponent(name)
        let contents = result.appendingPathComponent("Contents")
        try fm.createDirectory(at: contents.appendingPathComponent("MacOS"), withIntermediateDirectories: true)
        try fm.createDirectory(at: contents.appendingPathComponent("Resources"), withIntermediateDirectories: true)
        try fm.copyItem(at: URL(fileURLWithPath: "/usr/bin/true"), to: contents.appendingPathComponent("MacOS/THROW-UP"))
        var info: [String: Any] = ["CFBundleIdentifier": InstallerEngine.identifier,
            "CFBundleExecutable": "THROW-UP", "CFBundlePackageType": "BNDL",
            "CFBundleVersion": version, "CFBundleShortVersionString": version, "CFBundleName": "THROW-UP"]
        if format == .au {
            info["AudioComponents"] = [["type": audioUnit.type, "subtype": audioUnit.subtype, "manufacturer": audioUnit.manufacturer,
                                        "name": "VIVID FEVER DREAMS: THROW-UP", "version": 1, "factoryFunction": "Factory"]]
        } else {
            let module = """
            { "Classes": [
              { "CID": "\(componentID)", "Category": "Audio Module Class", },
              { "CID": "\(controllerID)", "Category": "Component Controller Class", },
            ], }
            """
            try module.write(to: contents.appendingPathComponent("Resources/moduleinfo.json"), atomically: true, encoding: .utf8)
        }
        try PropertyListSerialization.data(fromPropertyList: info, format: .xml, options: 0)
            .write(to: contents.appendingPathComponent("Info.plist"))
        try InstallerEngine.run("/usr/bin/codesign", ["--force", "--sign", "-", result.path])
        return result
    }
    let payloadRoot = root.appendingPathComponent("Payload")
    try fm.createDirectory(at: payloadRoot, withIntermediateDirectories: true)
    let payloadVST3 = try bundle("Payload/THROW-UP.vst3", version: "0.1.4")
    let payloadAU = try bundle("Payload/THROW-UP.component", version: "0.1.4", format: .au)
    let previous = try bundle("previous.vst3", version: "0.1.2")
    let previousAU = try bundle("previous.component", version: "0.1.2", format: .au)
    let manifest = PayloadManifest(format: 2, version: "0.1.4", bundleIdentifier: InstallerEngine.identifier,
        componentID: componentID, controllerID: controllerID, audioUnits: [InstallerEngine.audioUnit],
        vst3Files: try InstallerEngine.fileHashes(in: payloadVST3), audioUnitFiles: try InstallerEngine.fileHashes(in: payloadAU))
    let locations = InstallLocations.inside(root)
    let parent = locations.userVST3
    let destination = parent.appendingPathComponent("THROW-UP.vst3")
    let destinationAU = locations.userComponents.appendingPathComponent("THROW-UP.component")
    let system = locations.systemVST3.appendingPathComponent("THROW-UP.vst3")
    let recovery = locations.recovered
    var refreshCount = 0
    func engine(hosts: @escaping (Set<PluginFormat>) -> [String] = { _ in [] }, at place: InstallLocations = locations,
                payload: URL = payloadRoot) -> InstallerEngine {
        let result = InstallerEngine(payloadRoot: payload, manifest: manifest, locations: place, runningHosts: hosts)
        result.refreshAudioUnits = { refreshCount += 1; return "Audio Unit refresh recorded by the self-test." }
        return result
    }
    /// The VST3 folder's contents, ignoring the shared suite lock file.
    func visible(_ folder: URL = parent) throws -> [String] {
        try fm.contentsOfDirectory(atPath: folder.path).filter { $0 != InstallerEngine.suiteLockName }.sorted()
    }
    let preset = root.appendingPathComponent("User/Library/Application Support/THROW-UP/Presets/keep.json")
    try fm.createDirectory(at: preset.deletingLastPathComponent(), withIntermediateDirectories: true)
    try Data("saved user sound".utf8).write(to: preset)
    let initial = try engine(hosts: otherInstallerHosts).install()
    try expect(initial.installed == [.vst3] && initial.version == "0.1.4", "initial installation succeeds with another installer running")
    try expect(try InstallerEngine.fileHashes(in: destination) == manifest.vst3Files, "installed payload is byte-identical")
    try expect(try String(contentsOf: preset) == "saved user sound", "saved user presets remain untouched")
    try expect(!InstallerEngine.exists(locations.userComponents) && refreshCount == 0, "a VST3-only install leaves the Components folder alone")
    let reinstall = try engine().install()
    try expect(reinstall.skipped == [.vst3] && reinstall.updated.isEmpty, "an identical reinstall is skipped")
    try fm.removeItem(at: destination)
    try fm.copyItem(at: previous, to: destination)
    let update = try engine(hosts: otherInstallerHosts).install()
    try expect(update.updated == [.vst3] && (try InstallerEngine.identity(at: destination)).version == "0.1.4", "older version updates in place with another installer running")
    try expect(try visible() == ["THROW-UP.vst3"], "successful update leaves no old version to manage")

    let newer = try bundle("newer.vst3", version: "0.1.10")
    try fm.removeItem(at: destination)
    try fm.copyItem(at: newer, to: destination)
    let newerHashes = try InstallerEngine.fileHashes(in: destination)
    try rejects("numeric newer version blocks accidental downgrade") { _ = try engine().install() }
    try expect(try InstallerEngine.fileHashes(in: destination) == newerHashes, "downgrade refusal preserves newer bundle")

    try fm.removeItem(at: destination)
    try fm.copyItem(at: previous, to: destination)
    let oldHashes = try InstallerEngine.fileHashes(in: destination)
    let rollback = engine()
    rollback.failAfterCommitForTesting = true
    try rejects("post-commit failure triggers rollback") { _ = try rollback.install() }
    try expect(try InstallerEngine.fileHashes(in: destination) == oldHashes, "rollback restores exact prior bundle")
    try expect(try visible() == ["THROW-UP.vst3"], "rollback removes temporary new bundle")
    try rejects("a classified open audio host still blocks update") {
        _ = try engine(hosts: { formats in
            InstallerHostDetection.isAudioApplication(bundleIdentifier: "com.ableton.live",
                name: "Ableton Live", processID: 102, installerProcessID: installerPID, formats: formats) ? ["Ableton Live"] : []
        }).install()
    }
    try expect(try InstallerEngine.fileHashes(in: destination) == oldHashes, "host refusal leaves existing plugin unchanged")

    var hostCheckCount = 0
    try rejects("host starting during preparation blocks commit") {
        _ = try engine(hosts: { _ in hostCheckCount += 1; return hostCheckCount > 1 ? ["Test Host"] : [] }).install()
    }
    try expect(try InstallerEngine.fileHashes(in: destination) == oldHashes, "late host refusal preserves prior plugin")
    try fm.createDirectory(at: system.deletingLastPathComponent(), withIntermediateDirectories: true)
    try fm.copyItem(at: previous, to: system)
    try rejects("system-folder installation refuses duplicate") { _ = try engine().install() }
    try fm.removeItem(at: system)

    let infoURL = destination.appendingPathComponent("Contents/Info.plist")
    let oldInfo = try Data(contentsOf: infoURL)
    var wrongInfo = try PropertyListSerialization.propertyList(from: oldInfo, format: nil) as? [String: Any] ?? [:]
    wrongInfo["CFBundleIdentifier"] = "com.someone.unrelated"
    try PropertyListSerialization.data(fromPropertyList: wrongInfo, format: .xml, options: 0).write(to: infoURL)
    let unrelatedHashes = try InstallerEngine.fileHashes(in: destination)
    try rejects("unrelated bundle is never replaced") { _ = try engine().install() }
    try expect(try InstallerEngine.fileHashes(in: destination) == unrelatedHashes, "unrelated bundle remains byte-identical")
    try oldInfo.write(to: infoURL)

    let moduleURL = destination.appendingPathComponent("Contents/Resources/moduleinfo.json")
    let oldModule = try Data(contentsOf: moduleURL)
    try String(decoding: oldModule, as: UTF8.self).replacingOccurrences(of: componentID, with: "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA")
        .write(to: moduleURL, atomically: true, encoding: .utf8)
    let identityHashes = try InstallerEngine.fileHashes(in: destination)
    try rejects("different valid class identity blocks update") { _ = try engine().install() }
    try expect(try InstallerEngine.fileHashes(in: destination) == identityHashes, "different class identity stays untouched")
    try oldModule.write(to: moduleURL)

    try fm.removeItem(at: destination)
    try fm.createSymbolicLink(at: destination, withDestinationURL: previous)
    try rejects("symlink destination is refused") { _ = try engine().install() }
    try expect(try InstallerEngine.identity(at: previous).version == "0.1.2", "symlink target remains intact")
    try fm.removeItem(at: destination)
    let realFolder = root.appendingPathComponent("linked-target")
    try fm.createDirectory(at: realFolder, withIntermediateDirectories: true)
    var linkedLocations = InstallLocations.inside(root.appendingPathComponent("Linked"))
    try fm.createDirectory(at: linkedLocations.userVST3.deletingLastPathComponent(), withIntermediateDirectories: true)
    try fm.createSymbolicLink(at: linkedLocations.userVST3, withDestinationURL: realFolder)
    linkedLocations.recovered = recovery
    try rejects("symlink ancestor is refused") { _ = try engine(at: linkedLocations).install() }
    try expect(try fm.contentsOfDirectory(atPath: realFolder.path).isEmpty, "symlink folder receives no files")

    let tamperURL = payloadVST3.appendingPathComponent("Contents/Resources/extra.txt")
    try Data("unexpected".utf8).write(to: tamperURL)
    try rejects("extra payload file fails integrity check") { _ = try engine().install() }
    try fm.removeItem(at: tamperURL)
    try fm.createSymbolicLink(at: tamperURL, withDestinationURL: previous)
    try rejects("internal payload symlink is refused") { _ = try engine().install() }
    try fm.removeItem(at: tamperURL)
    let freshRollback = engine()
    freshRollback.failAfterCommitForTesting = true
    try rejects("initial-install verification failure rolls back") { _ = try freshRollback.install() }
    try expect(!InstallerEngine.exists(destination), "failed initial install leaves no plugin")
    try expect(try visible().isEmpty, "all failed transactions clean up staging")
    let priorPlugin = parent.appendingPathComponent("SHIFTY.vst3")
    let otherPlugin = parent.appendingPathComponent("YOI-BOI.vst3")
    for item in [priorPlugin, otherPlugin] {
        try fm.createDirectory(at: item, withIntermediateDirectories: true)
        try Data("existing plug-in content".utf8).write(to: item.appendingPathComponent("keep.txt"))
    }
    let alongside = try engine().install()
    try expect(alongside.installed == [.vst3], "THROW-UP installs beside previously installed plug-ins")
    try expect(try String(contentsOf: priorPlugin.appendingPathComponent("keep.txt")) == "existing plug-in content",
        "existing SHIFTY plug-in stays untouched")
    try expect(try String(contentsOf: otherPlugin.appendingPathComponent("keep.txt")) == "existing plug-in content",
        "existing YOI-BOI plug-in stays untouched")

    // Audio Unit: installs into Components, verifies the AU codes, and refreshes the AU list.
    let both = try engine().install(formats: [.vst3, .au])
    try expect(both.installed == [.au] && both.skipped == [.vst3] && both.audioUnitsRefreshed && refreshCount == 1,
               "ticking AU installs the Audio Unit into Components, skips the identical VST3 and refreshes the AU list once")
    try expect(try InstallerEngine.fileHashes(in: destinationAU) == manifest.audioUnitFiles
               && (try InstallerEngine.identity(at: destinationAU, format: .au)).audioUnits == [InstallerEngine.audioUnit],
               "the installed AU is byte-identical and registers aumf/Thr1/Yoib")
    try fm.removeItem(at: destinationAU)
    try fm.copyItem(at: previousAU, to: destinationAU)
    let auUpdate = try engine().install(formats: [.au])
    try expect(auUpdate.updated == [.au] && refreshCount == 2, "an older AU updates in place")
    try fm.removeItem(at: destinationAU)
    let otherCodes = try bundle("other-codes.component", version: "0.1.2", format: .au,
                                audioUnit: AudioUnitIdentity(type: InstallerEngine.audioUnit.type, subtype: "Zzz9", manufacturer: InstallerEngine.audioUnit.manufacturer))
    try fm.copyItem(at: otherCodes, to: destinationAU)
    try rejectsWith("an installed AU with different codes is never replaced", containing: "different Audio Unit codes") { _ = try engine().install(formats: [.au]) }
    try fm.removeItem(at: destinationAU)
    try fm.createDirectory(at: locations.systemComponents, withIntermediateDirectories: true)
    try fm.copyItem(at: previousAU, to: locations.systemComponents.appendingPathComponent("THROW-UP.component"))
    try rejectsWith("an AU in /Library/Audio/Plug-Ins/Components blocks the AU install", containing: "system AU folder") { _ = try engine().install(formats: [.au]) }
    try expect((try engine().install(formats: [.vst3])).skipped == [.vst3], "the system AU does not block the VST3 format")
    try fm.removeItem(at: locations.systemComponents.appendingPathComponent("THROW-UP.component"))
    try rejectsWith("Logic Pro blocks an AU install", containing: "Logic Pro") {
        _ = try engine(hosts: { $0.contains(.au) ? ["Logic Pro"] : [] }).install(formats: [.vst3, .au])
    }
    try expect(!InstallerEngine.exists(destinationAU), "the refused AU install changed nothing")
    try expect(!InstallerHostDetection.isAudioApplication(bundleIdentifier: "com.apple.logic10", name: "Logic Pro", processID: 102,
                                                          installerProcessID: installerPID, formats: [.vst3])
               && InstallerHostDetection.isAudioApplication(bundleIdentifier: "com.apple.garageband10", name: nil, processID: 102,
                                                            installerProcessID: installerPID, formats: [.au]),
               "Logic Pro and GarageBand count as hosts only for an AU install")
    try fm.removeItem(at: destination)
    try fm.copyItem(at: previous, to: destination)
    let twoFormatRollback = engine()
    twoFormatRollback.failAfterCommitForTesting = true
    try rejects("a failure after committing both formats rolls both back") { _ = try twoFormatRollback.install(formats: [.vst3, .au]) }
    try expect(try InstallerEngine.fileHashes(in: destination) == oldHashes && !InstallerEngine.exists(destinationAU)
               && (try visible(locations.userComponents)).isEmpty, "both formats are back to their previous state")
    try rejectsWith("an empty selection is refused", containing: "Tick VST3, AU or both") { _ = try engine().install(formats: []) }
    let noComponents = try bundle("no-components.component", version: "0.1.4", format: .au)
    var noInfo = try PropertyListSerialization.propertyList(from: Data(contentsOf: noComponents.appendingPathComponent("Contents/Info.plist")), format: nil) as? [String: Any] ?? [:]
    noInfo["AudioComponents"] = [[String: Any]]()
    try PropertyListSerialization.data(fromPropertyList: noInfo, format: .xml, options: 0).write(to: noComponents.appendingPathComponent("Contents/Info.plist"))
    try rejects("an AU bundle without AudioComponents entries is not accepted") { _ = try InstallerEngine.identity(at: noComponents, format: .au) }

    // P1: the download quarantine flag never reaches the plug-in folders.
    func allPaths(_ folder: URL) -> [String] {
        [folder.path] + (fm.enumerator(atPath: folder.path)?.compactMap { $0 as? String }.map { folder.path + "/" + $0 } ?? [])
    }
    func hasQuarantine(_ path: String) -> Bool { getxattr(path, "com.apple.quarantine", nil, 0, 0, XATTR_NOFOLLOW) >= 0 }
    try fm.removeItem(at: destination)
    let quarantinedPayload = root.appendingPathComponent("QuarantinedPayload")
    try fm.copyItem(at: payloadRoot, to: quarantinedPayload)
    let flag = Array("0083;00000000;Safari;".utf8)
    for path in allPaths(quarantinedPayload) {
        guard flag.withUnsafeBytes({ setxattr(path, "com.apple.quarantine", $0.baseAddress, $0.count, 0, XATTR_NOFOLLOW) }) == 0 else {
            throw InstallError.message("Cannot set the fixture quarantine flag on \(path)")
        }
    }
    _ = try engine(payload: quarantinedPayload).install(formats: [.vst3, .au])
    try expect(allPaths(quarantinedPayload).allSatisfy(hasQuarantine) && !allPaths(destination).contains(where: hasQuarantine)
               && !allPaths(destinationAU).contains(where: hasQuarantine),
               "a quarantined payload installs without passing its quarantine flag to the installed VST3 or AU")

    // P2: launch arguments.
    try expect(LaunchMode.parse(["app"]) == .gui && LaunchMode.parse(["app", "-psn_0_1234567"]) == .gui
               && LaunchMode.parse(["app", "-NSDocumentRevisionsDebugMode", "YES"]) == .gui,
               "the window opens even when macOS adds a -psn_ or runtime argument")
    try expect(LaunchMode.parse(["app", "-psn_0_1", "--self-test"]) == .selfTest && LaunchMode.parse(["app", "--bogus"]) == .invalid
               && LaunchMode.parse(["app", "--make-manifest", "a", "b", "1.0"]) == .makeManifest(payload: "a", manifest: "b", version: "1.0")
               && LaunchMode.parse(["app", "--check-whats-new", "n.json"]) == .checkWhatsNew("n.json"),
               "known options parse and unknown -- options are rejected")

    // P3: the temporary copies are never .vst3 or .component bundles.
    try fm.removeItem(at: destination)
    try fm.removeItem(at: destinationAU)
    var stagedNames: [String] = []
    let naming = engine()
    naming.afterStagingForTesting = {
        stagedNames = try (fm.contentsOfDirectory(atPath: parent.path) + fm.contentsOfDirectory(atPath: locations.userComponents.path)).filter { $0.contains("-stage-") }
    }
    _ = try naming.install(formats: [.vst3, .au])
    try expect(stagedNames.count == 2 && stagedNames.allSatisfy { $0.hasPrefix(".throwup-stage-") && !$0.contains(".vst3") && !$0.contains(".component") },
               "the temporary copies have no plug-in extension, so audio apps never scan them")

    // P3 + P6: a rollback that cannot be done safely keeps the previous copy outside the plug-in folders.
    try fm.removeItem(at: destination)
    try fm.copyItem(at: previous, to: destination)
    let previousHashes = try InstallerEngine.fileHashes(in: destination)
    let outsideEdit = destination.appendingPathComponent("Contents/Resources/outside.txt")
    let interfered = engine()
    interfered.afterCommitForTesting = { format in if format == .vst3 { try Data("changed by another program".utf8).write(to: outsideEdit) } }
    interfered.failAfterCommitForTesting = true
    try rejectsWith("a rollback blocked by an outside change reports where the previous copy was saved", containing: "was saved to") {
        _ = try interfered.install()
    }
    let recovered = (try? fm.contentsOfDirectory(at: recovery, includingPropertiesForKeys: nil)) ?? []
    try expect(recovered.count == 1 && recovered[0].pathExtension == "vst3" && (try InstallerEngine.fileHashes(in: recovered[0])) == previousHashes,
               "the previous copy is saved, intact, in the Recovered folder outside the plug-in folders")
    try expect(InstallerEngine.exists(outsideEdit) && !(try visible()).contains { $0.contains("-stage-") },
               "the changed destination is never swapped back, and nothing is left in the VST3 folder")
    try fm.removeItem(at: destination)
    let freshInterfered = engine()
    freshInterfered.afterCommitForTesting = { _ in try Data("changed by another program".utf8).write(to: outsideEdit) }
    freshInterfered.failAfterCommitForTesting = true
    try rejects("an initial install changed by another program reports failure") { _ = try freshInterfered.install() }
    try expect(InstallerEngine.exists(outsideEdit), "an initial install changed by another program is left in place, not removed")
    try fm.removeItem(at: destination)

    // P5: the suite installer's lock and unfinished transactions are respected, in both plug-in folders.
    let suiteLeftover = locations.userComponents.appendingPathComponent(InstallerEngine.suiteTransactionPrefix + UUID().uuidString)
    try fm.createDirectory(at: suiteLeftover, withIntermediateDirectories: false)
    try rejectsWith("an interrupted suite installation blocks a single-plugin change", containing: "Vivid Fever Dreams Suite installer") {
        _ = try engine().install()
    }
    try expect(!InstallerEngine.exists(destination) && InstallerEngine.exists(suiteLeftover), "the suite's recovery folder and the plug-in folders are untouched")
    try fm.removeItem(at: suiteLeftover)
    let held = open(parent.appendingPathComponent(InstallerEngine.suiteLockName).path, O_CREAT | O_RDWR, 0o600)
    guard held >= 0, flock(held, LOCK_EX | LOCK_NB) == 0 else { throw InstallError.message("Cannot hold the fixture lock") }
    try rejectsWith("a suite installation in progress blocks a single-plugin change", containing: "Another Vivid Fever Dreams installer") {
        _ = try engine().install(formats: [.au])
    }
    flock(held, LOCK_UN); close(held)
    try expect((try engine().install()).installed == [.vst3], "installation proceeds once the shared lock is free")

    // P8: same host list as the suite installer.
    func host(_ identifier: String?, _ name: String?) -> Bool {
        InstallerHostDetection.isAudioApplication(bundleIdentifier: identifier, name: name, processID: 102, installerProcessID: installerPID)
    }
    try expect(!host("com.apple.logic10", "Logic Pro") && !host("com.apple.garageband10", "GarageBand") && !host("com.avid.ProTools", "Pro Tools")
               && !host("com.steinberg.activationmanager", "Steinberg Activation Manager") && !host("com.yoiboi.throwup", "THROW-UP"),
               "VST3-only changes ignore AU-only hosts, vendor utilities and the plug-in's own identifier")
    try expect(host("com.steinberg.cubase13", nil) && host("com.bitwig.BitwigStudio", nil) && host(nil, "Gig Performer 5") && host(nil, "Maschine 2"),
               "VST3 hosts are detected by identifier or name")
    let holder = Process()
    holder.executableURL = URL(fileURLWithPath: "/usr/bin/tail")
    let heldFile = root.appendingPathComponent("held.bin")
    try Data("held".utf8).write(to: heldFile)
    holder.arguments = ["-f", heldFile.path]
    holder.standardOutput = FileHandle.nullDevice
    try holder.run()
    var users: [String] = []
    for _ in 0..<50 {
        users = InstallerHostDetection.openFileUsers(paths: [heldFile.path, previous.path + "/Contents/Info.plist"], excluding: [])
        if !users.isEmpty { break }
        usleep(100_000)
    }
    holder.terminate(); holder.waitUntilExit()
    try expect(users.contains("tail"), "the open-file check finds a process holding the plug-in even when another listed path is not open")

    // What's new and rescan guidance (shared with the suite installer).
    let notes = try JSONSerialization.data(withJSONObject: ["THROW-UP": ["Now also an Audio Unit."], "OTHER PLUG-IN": ["Something else."]])
    try expect((try WhatsNew.parse(notes, requiredNames: ["THROW-UP"], allowOtherNames: true))["THROW-UP"] == ["Now also an Audio Unit."],
               "the installer reads its own entry from the suite's What’s new file")
    try rejects("What’s new without an entry for THROW-UP is refused") {
        _ = try WhatsNew.parse(try JSONSerialization.data(withJSONObject: ["OTHER PLUG-IN": ["x"]]), requiredNames: ["THROW-UP"], allowOtherNames: true)
    }
    let guidance = RescanGuidance.steps(installedBundleIdentifiers: ["com.ableton.live", "com.apple.logic10"], formats: [.vst3])
    try expect(guidance.count == 2 && guidance[0].contains("Rescan") && guidance[1].contains("Tick AU"),
               "rescan guidance covers Ableton Live and tells Logic users to tick AU")
    print("Installer self-test passed: \(count) checks. All writes stayed in \(root.path).")
}
