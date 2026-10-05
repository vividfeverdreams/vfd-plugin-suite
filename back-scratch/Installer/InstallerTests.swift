import Foundation
import Darwin

func runInstallerTests() throws {
    let fm = FileManager.default
    guard let canonicalTemp = realpath(fm.temporaryDirectory.path, nil) else {
        throw InstallError.message("Cannot resolve the temporary test directory.")
    }
    let temporaryRoot = String(cString: canonicalTemp)
    free(canonicalTemp)
    let root = URL(fileURLWithPath: temporaryRoot).appendingPathComponent("backscratch-installer-tests-\(UUID().uuidString)")
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
    let installerPID: Int32 = 100
    let otherInstallerPID: Int32 = 101
    try expect(!InstallerHostDetection.isAudioApplication(bundleIdentifier: "com.yoiboi.backscratch.installer",
        name: "BACK-SCRATCH-Installer", processID: otherInstallerPID, installerProcessID: installerPID),
        "a second BACK-SCRATCH installer is not mistaken for an audio host")
    try expect(!InstallerHostDetection.isAudioApplication(bundleIdentifier: "COM.YOIBOI.BACK-SCRATCH.INSTALLER",
        name: "Install BACK-SCRATCH", processID: otherInstallerPID, installerProcessID: installerPID),
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
    try expect(!InstallerHostDetection.isAudioApplication(bundleIdentifier: "com.yoiboi.backscratch.utility",
        name: "BACK-SCRATCH Utility", processID: 102, installerProcessID: installerPID),
        "sharing BACK-SCRATCH's bundle prefix does not make an app an audio host")
    try expect(!InstallerHostDetection.isAudioApplication(bundleIdentifier: nil,
        name: nil, processID: 102, installerProcessID: installerPID),
        "an application with no identifying metadata is not falsely blocked")
    let openFileOutput = """
    cUnassociated command
    p100
    cBACK-SCRATCH-Installer
    p101
    cBACK-SCRATCH-Installer
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
    let otherInstallerHosts: () -> [String] = {
        InstallerHostDetection.isAudioApplication(bundleIdentifier: "com.yoiboi.backscratch.installer",
            name: "BACK-SCRATCH-Installer", processID: otherInstallerPID, installerProcessID: installerPID)
            ? ["BACK-SCRATCH-Installer"] : []
    }
    func bundle(_ name: String, version: String) throws -> URL {
        let result = root.appendingPathComponent(name)
        let contents = result.appendingPathComponent("Contents")
        try fm.createDirectory(at: contents.appendingPathComponent("MacOS"), withIntermediateDirectories: true)
        try fm.createDirectory(at: contents.appendingPathComponent("Resources"), withIntermediateDirectories: true)
        try fm.copyItem(at: URL(fileURLWithPath: "/usr/bin/true"), to: contents.appendingPathComponent("MacOS/BACK-SCRATCH"))
        let info: [String: Any] = ["CFBundleIdentifier": InstallerEngine.identifier,
            "CFBundleExecutable": "BACK-SCRATCH", "CFBundlePackageType": "BNDL",
            "CFBundleVersion": version, "CFBundleShortVersionString": version, "CFBundleName": "BACK-SCRATCH"]
        try PropertyListSerialization.data(fromPropertyList: info, format: .xml, options: 0)
            .write(to: contents.appendingPathComponent("Info.plist"))
        let module = """
        { "Classes": [
          { "CID": "\(componentID)", "Category": "Audio Module Class", },
          { "CID": "\(controllerID)", "Category": "Component Controller Class", },
        ], }
        """
        try module.write(to: contents.appendingPathComponent("Resources/moduleinfo.json"), atomically: true, encoding: .utf8)
        try InstallerEngine.run("/usr/bin/codesign", ["--force", "--sign", "-", result.path])
        return result
    }
    let payload = try bundle("payload.vst3", version: "0.1.4")
    let previous = try bundle("previous.vst3", version: "0.1.2")
    let manifest = PayloadManifest(format: 1, version: "0.1.4", bundleIdentifier: InstallerEngine.identifier,
        componentID: componentID, controllerID: controllerID,
        files: try InstallerEngine.fileHashes(in: payload))
    let parent = root.appendingPathComponent("User/Library/Audio/Plug-Ins/VST3")
    let destination = parent.appendingPathComponent("BACK-SCRATCH.vst3")
    let system = root.appendingPathComponent("System/BACK-SCRATCH.vst3")
    func engine(hosts: @escaping () -> [String] = { [] }) -> InstallerEngine {
        InstallerEngine(payload: payload, manifest: manifest, destination: destination,
                        systemCopy: system, runningHosts: hosts)
    }
    let preset = root.appendingPathComponent("User/Library/Application Support/BACK-SCRATCH/Presets/keep.json")
    try fm.createDirectory(at: preset.deletingLastPathComponent(), withIntermediateDirectories: true)
    try Data("saved user sound".utf8).write(to: preset)
    let initial = try engine(hosts: otherInstallerHosts).install()
    try expect(!initial.updated && initial.version == "0.1.4", "initial installation succeeds with another installer running")
    try expect(try InstallerEngine.fileHashes(in: destination) == manifest.files, "installed payload is byte-identical")
    try expect(try String(contentsOf: preset) == "saved user sound", "saved user presets remain untouched")
    let reinstall = try engine().install()
    try expect(reinstall.updated, "same-version reinstall replaces in place")
    try fm.removeItem(at: destination)
    try fm.copyItem(at: previous, to: destination)
    let update = try engine(hosts: otherInstallerHosts).install()
    try expect(update.updated && (try InstallerEngine.identity(at: destination)).version == "0.1.4", "older version updates in place with another installer running")
    try expect(try fm.contentsOfDirectory(atPath: parent.path) == ["BACK-SCRATCH.vst3"], "successful update leaves no old version to manage")

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
    try expect(try fm.contentsOfDirectory(atPath: parent.path) == ["BACK-SCRATCH.vst3"], "rollback removes temporary new bundle")
    try rejects("a classified open audio host still blocks update") {
        _ = try engine(hosts: {
            InstallerHostDetection.isAudioApplication(bundleIdentifier: "com.ableton.live",
                name: "Ableton Live", processID: 102, installerProcessID: installerPID) ? ["Ableton Live"] : []
        }).install()
    }
    try expect(try InstallerEngine.fileHashes(in: destination) == oldHashes, "host refusal leaves existing plugin unchanged")

    var hostCheckCount = 0
    try rejects("host starting during preparation blocks commit") {
        _ = try engine(hosts: { hostCheckCount += 1; return hostCheckCount > 1 ? ["Test Host"] : [] }).install()
    }
    try expect(try InstallerEngine.fileHashes(in: destination) == oldHashes, "late host refusal preserves prior plugin")
    try fm.createDirectory(at: system.deletingLastPathComponent(), withIntermediateDirectories: true)
    try fm.copyItem(at: previous, to: system)
    try rejects("system-folder installation refuses duplicate") { _ = try engine().install() }
    try fm.removeItem(at: system)

    let infoURL = destination.appendingPathComponent("Contents/Info.plist")
    let oldInfo = try Data(contentsOf: infoURL)
    var wrongInfo = try PropertyListSerialization.propertyList(from: oldInfo, format: nil) as! [String: Any]
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
    try rejects("wrong VST3 class identity is refused") { _ = try engine().install() }
    try oldModule.write(to: moduleURL)

    let differentIdentity = String(decoding: oldModule, as: UTF8.self)
        .replacingOccurrences(of: componentID, with: "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA")
    try differentIdentity.write(to: moduleURL, atomically: true, encoding: .utf8)
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
    let linkedParent = root.appendingPathComponent("linked-parent")
    try fm.createSymbolicLink(at: linkedParent, withDestinationURL: realFolder)
    let linkedEngine = InstallerEngine(payload: payload, manifest: manifest,
        destination: linkedParent.appendingPathComponent("BACK-SCRATCH.vst3"), systemCopy: system, runningHosts: { [] })
    try rejects("symlink ancestor is refused") { _ = try linkedEngine.install() }
    try expect(try fm.contentsOfDirectory(atPath: realFolder.path).isEmpty, "symlink folder receives no files")

    let tamperURL = payload.appendingPathComponent("Contents/Resources/extra.txt")
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
    try expect(try fm.contentsOfDirectory(atPath: parent.path).isEmpty, "all failed transactions clean up staging")
    print("Installer self-test passed: \(count) checks. All writes stayed in \(root.path).")
}
