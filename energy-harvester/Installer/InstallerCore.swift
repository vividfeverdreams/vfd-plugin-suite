import Foundation
import CryptoKit
import Darwin

enum InstallError: LocalizedError {
    case message(String)
    var errorDescription: String? { if case .message(let value) = self { return value }; return nil }
}

struct PayloadManifest: Codable {
    let format: Int
    let version: String
    let bundleIdentifier: String
    let componentID: String
    let controllerID: String
    let files: [String: String]
}

struct InstallResult {
    let updated: Bool
    let version: String
    let destination: URL
    let log: [String]
}

struct PluginIdentity: Equatable {
    let version: String
    let componentID: String
    let controllerID: String
}

/// Command-line modes. Anything without a recognised "--" option opens the window, because LaunchServices can
/// append arguments such as -psn_0_NNNN on a Gatekeeper-approved first launch.
enum LaunchMode: Equatable {
    case gui, selfTest, inspectPayload, checkHosts
    case makeManifest(payload: String, manifest: String, version: String)
    case invalid
    static func parse(_ rawArguments: [String]) -> LaunchMode {
        let arguments = Array(rawArguments.dropFirst().filter { !$0.hasPrefix("-psn_") })
        guard let flag = arguments.first(where: { $0.hasPrefix("--") }) else { return .gui }
        guard arguments.first == flag else { return .invalid }
        switch (flag, arguments.count) {
        case ("--self-test", 1): return .selfTest
        case ("--inspect-payload", 1): return .inspectPayload
        case ("--check-hosts", 1): return .checkHosts
        case ("--make-manifest", 4): return .makeManifest(payload: arguments[1], manifest: arguments[2], version: arguments[3])
        default: return .invalid
        }
    }
}

/// Same host rules as the Vivid Fever Dreams Suite installer.
enum InstallerHostDetection {
    static func isInstaller(_ identifier: String?) -> Bool {
        let id = (identifier ?? "").lowercased()
        return id == "xyz.vividfeverdreams.suite.installer" || id == "com.yoiboi.installer" ||
            (id.hasPrefix("com.yoiboi.") && id.hasSuffix(".installer"))
    }
    /// VST3-capable hosts with stable bundle identifiers (compared lowercased).
    static let hostIdentifiers: Set<String> = [
        "com.ableton.live",          // Ableton Live 9–12: Suite, Standard, Intro and Lite
        "com.bitwig.bitwigstudio",   // Bitwig Studio
        "com.cockos.reaper",         // REAPER
        "com.image-line.flstudio",   // FL Studio
        "com.cycling74.max",         // Max
        "com.tracktion.pluginval"    // pluginval
    ]
    /// Product-level prefixes for hosts whose identifiers carry a version number, such as com.steinberg.cubase13.
    static let hostIdentifierPrefixes = ["com.steinberg.cubase", "com.steinberg.nuendo", "com.presonus.studioone",
                                         "com.motu.digitalperformer", "com.tracktion.waveform"]
    /// Application names, matched exactly or followed by a version number ("Gig Performer 5", "Cubase 13").
    /// Hosts not listed here are still caught by the open-file check while they have the plug-in loaded.
    static let hostNames = ["ableton live", "bitwig studio", "reaper", "fl studio", "cubase", "nuendo", "studio one",
                            "digital performer", "renoise", "reason", "maschine 2", "komplete kontrol", "gig performer",
                            "vienna ensemble pro", "waveform", "pluginval"]

    static func isAudioApplication(bundleIdentifier: String?, name: String?,
                                   processID: Int32, installerProcessID: Int32) -> Bool {
        guard processID != installerProcessID, !isInstaller(bundleIdentifier) else { return false }
        let id = (bundleIdentifier ?? "").lowercased()
        if hostIdentifiers.contains(id) || hostIdentifierPrefixes.contains(where: id.hasPrefix) { return true }
        let applicationName = (name ?? "").lowercased()
        return hostNames.contains { hostName in
            if applicationName == hostName { return true }
            guard applicationName.hasPrefix(hostName + " ") else { return false }
            return applicationName.dropFirst(hostName.count + 1).first?.isNumber == true
        }
    }

    static func fileUsers(_ output: String, excluding processIDs: Set<Int32>) -> [String] {
        var processID: Int32?
        var names: [String] = []
        for line in output.split(separator: "\n") {
            if line.first == "p" { processID = Int32(line.dropFirst()) }
            else if line.first == "c", let id = processID, !processIDs.contains(id) {
                names.append(String(line.dropFirst()))
            }
        }
        return names
    }

    /// Names of processes that have any of `paths` open or mapped. lsof exits 1 whenever at least one listed path
    /// is not open; the records it prints for the open paths are still valid, so status 1 is accepted.
    static func openFileUsers(paths: [String], excluding processIDs: Set<Int32>) -> [String] {
        guard !paths.isEmpty else { return [] }
        let process = Process()
        let output = Pipe()
        process.executableURL = URL(fileURLWithPath: "/usr/sbin/lsof")
        process.arguments = ["-w", "+c", "0", "-Fpc", "--"] + paths
        process.standardOutput = output
        process.standardError = FileHandle.nullDevice
        do { try process.run() } catch { return [] }
        let data = output.fileHandleForReading.readDataToEndOfFile()
        process.waitUntilExit()
        guard process.terminationStatus == 0 || process.terminationStatus == 1 else { return [] }
        return fileUsers(String(decoding: data, as: UTF8.self), excluding: processIDs)
    }
}

/// The engine receives explicit paths so its entire transaction can be exercised in a temporary directory.
/// The shipping UI always supplies the standard per-user VST3 location.
final class InstallerEngine {
    static let identifier = "com.yoiboi.energyharvester"
    static let executable = "ENERGY HARVESTER"
    /// Shared with the Vivid Fever Dreams Suite installer, so the two never change the VST3 folder at the same time.
    static let suiteLockName = ".vfd-suite-installer.lock"
    static let suiteTransactionPrefix = ".vfd-suite-transaction-"
    /// Build-time guard for --make-manifest: the released VST3 class IDs (nil when not pinned).
    static let pinnedIdentity: (componentID: String, controllerID: String)? = nil
    private let fm = FileManager.default
    let payload: URL
    let manifest: PayloadManifest
    let destination: URL
    let systemCopy: URL
    /// Where a previous copy is kept if a rollback cannot finish: outside every folder audio apps scan.
    let recoveryRoot: URL
    let runningHosts: () -> [String]
    var log: [String] = []
    // Test fault injection never appears in the app's command line or UI.
    var failAfterCommitForTesting = false
    var afterStagingForTesting: (() throws -> Void)?
    var afterCommitForTesting: (() throws -> Void)?

    init(payload: URL, manifest: PayloadManifest, destination: URL, systemCopy: URL, recoveryRoot: URL,
         runningHosts: @escaping () -> [String]) {
        // The payload is only read, so it may be reached through a symlinked folder.
        self.payload = Self.canonical(payload)
        self.manifest = manifest
        self.destination = destination
        self.systemCopy = systemCopy
        self.recoveryRoot = recoveryRoot
        self.runningHosts = runningHosts
    }

    static func readManifest(_ url: URL) throws -> PayloadManifest {
        try JSONDecoder().decode(PayloadManifest.self, from: Data(contentsOf: url))
    }

    static func canonical(_ url: URL) -> URL {
        guard let resolved = realpath(url.path, nil) else { return url }
        defer { free(resolved) }
        return URL(fileURLWithPath: String(cString: resolved), isDirectory: true)
    }

    static func exists(_ url: URL) -> Bool {
        var info = stat()
        return lstat(url.path, &info) == 0
    }

    static func refuseSymlinkAncestors(_ url: URL) throws {
        var current = URL(fileURLWithPath: "/", isDirectory: true)
        for component in url.pathComponents.dropFirst() {
            current.appendPathComponent(component)
            var info = stat()
            if lstat(current.path, &info) == 0 {
                guard (info.st_mode & S_IFMT) != S_IFLNK else {
                    throw InstallError.message("Installation stopped because this path is a symbolic link: \(current.path). Use a normal folder for ENERGY HARVESTER.")
                }
            } else if errno != ENOENT {
                throw InstallError.message("Cannot inspect \(current.path): \(String(cString: strerror(errno)))")
            }
        }
    }

    static func fileHashes(in bundle: URL) throws -> [String: String] {
        try refuseSymlinkAncestors(bundle)
        let fm = FileManager.default
        var enumerationError: Error?
        guard let enumerator = fm.enumerator(at: bundle, includingPropertiesForKeys: nil,
                                            errorHandler: { _, error in enumerationError = error; return false }) else {
            throw InstallError.message("The ENERGY HARVESTER payload cannot be read.")
        }
        var hashes: [String: String] = [:]
        for case let url as URL in enumerator {
            var info = stat()
            guard lstat(url.path, &info) == 0 else { throw InstallError.message("Cannot inspect \(url.lastPathComponent).") }
            let type = info.st_mode & S_IFMT
            guard type == S_IFREG || type == S_IFDIR else {
                throw InstallError.message("Unexpected link or special file inside \(bundle.lastPathComponent).")
            }
            if type == S_IFREG {
                let relative = String(url.path.dropFirst(bundle.path.count + 1))
                let digest = SHA256.hash(data: try Data(contentsOf: url, options: .mappedIfSafe))
                hashes[relative] = digest.map { String(format: "%02x", $0) }.joined()
            }
        }
        if let error = enumerationError { throw error }
        guard !hashes.isEmpty else { throw InstallError.message("The ENERGY HARVESTER payload is empty.") }
        return hashes
    }

    /// Clears the download quarantine flag from the staged copy so Gatekeeper does not block the plug-in when a DAW
    /// loads it. Extended attributes are not part of the SHA-256 inventory or the code signature.
    static func stripQuarantine(in root: URL) throws {
        func strip(_ path: String) throws {
            if removexattr(path, "com.apple.quarantine", XATTR_NOFOLLOW) != 0 && errno != ENOATTR {
                throw InstallError.message("Could not clear the download quarantine flag on \(path): \(String(cString: strerror(errno)))")
            }
        }
        try strip(root.path)
        guard let enumerator = FileManager.default.enumerator(atPath: root.path) else {
            throw InstallError.message("The ENERGY HARVESTER payload cannot be read.")
        }
        for case let relative as String in enumerator { try strip(root.path + "/" + relative) }
    }

    @discardableResult
    static func run(_ executable: String, _ arguments: [String]) throws -> String {
        let process = Process()
        let output = Pipe()
        process.executableURL = URL(fileURLWithPath: executable)
        process.arguments = arguments
        process.standardOutput = output
        process.standardError = output
        try process.run()
        let data = output.fileHandleForReading.readDataToEndOfFile()
        process.waitUntilExit()
        let text = String(data: data, encoding: .utf8) ?? ""
        guard process.terminationStatus == 0 else {
            throw InstallError.message("\(URL(fileURLWithPath: executable).lastPathComponent) could not verify the operation. \(text.trimmingCharacters(in: .whitespacesAndNewlines))")
        }
        return text
    }

    static func identity(at bundle: URL) throws -> PluginIdentity {
        try refuseSymlinkAncestors(bundle)
        let plist = bundle.appendingPathComponent("Contents/Info.plist")
        let object = try PropertyListSerialization.propertyList(from: Data(contentsOf: plist), format: nil)
        guard let info = object as? [String: Any],
              info["CFBundleIdentifier"] as? String == identifier,
              info["CFBundleExecutable"] as? String == executable,
              let version = info["CFBundleShortVersionString"] as? String else {
            throw InstallError.message("The existing item is not the expected ENERGY HARVESTER plug-in. It has been left untouched.")
        }
        // JUCE's VST3 metadata permits trailing commas; normalize those before parsing it.
        let module = bundle.appendingPathComponent("Contents/Resources/moduleinfo.json")
        let text = try String(contentsOf: module, encoding: .utf8)
        let expression = try NSRegularExpression(pattern: ",\\s*([}\\]])")
        let normalized = expression.stringByReplacingMatches(in: text,
            range: NSRange(text.startIndex..., in: text), withTemplate: "$1")
        guard let data = normalized.data(using: .utf8),
              let moduleInfo = try JSONSerialization.jsonObject(with: data) as? [String: Any],
              let classes = moduleInfo["Classes"] as? [[String: Any]],
              let component = classes.first(where: { $0["Category"] as? String == "Audio Module Class" })?["CID"] as? String,
              let controller = classes.first(where: { $0["Category"] as? String == "Component Controller Class" })?["CID"] as? String,
              component.range(of: "^[0-9A-Fa-f]{32}$", options: .regularExpression) != nil,
              controller.range(of: "^[0-9A-Fa-f]{32}$", options: .regularExpression) != nil,
              component.uppercased() != controller.uppercased() else {
            throw InstallError.message("The VST3 identity does not match ENERGY HARVESTER. No installed plug-in was changed.")
        }
        return PluginIdentity(version: version, componentID: component.uppercased(),
                              controllerID: controller.uppercased())
    }

    func verifyPayload(at bundle: URL) throws {
        guard manifest.format == 1, manifest.bundleIdentifier == Self.identifier else {
            throw InstallError.message("The installer manifest does not describe this ENERGY HARVESTER plug-in.")
        }
        let identity = try Self.identity(at: bundle)
        guard identity.version == manifest.version,
              identity.componentID == manifest.componentID,
              identity.controllerID == manifest.controllerID else {
            throw InstallError.message("The plug-in identity does not match the installer.")
        }
        guard fm.isExecutableFile(atPath: bundle.appendingPathComponent("Contents/MacOS/ENERGY HARVESTER").path) else {
            throw InstallError.message("The plug-in executable does not have executable permissions.")
        }
        guard try Self.fileHashes(in: bundle) == manifest.files else {
            throw InstallError.message("The plug-in files did not pass their integrity check. Download a fresh installer and try again.")
        }
        try Self.run("/usr/bin/codesign", ["--verify", "--deep", "--strict", bundle.path])
    }

    private func checkHosts() throws {
        let hosts = runningHosts().sorted()
        guard hosts.isEmpty else {
            throw InstallError.message("Quit \(hosts.joined(separator: ", ")) and click Install / Update again. Your current plug-in has not been changed.")
        }
    }

    /// The suite installer's lock file, so a single-plugin update and a suite transaction never overlap.
    static func acquireSuiteLock(in folder: URL) throws -> Int32 {
        let lockURL = folder.appendingPathComponent(suiteLockName)
        let lock = open(lockURL.path, O_CREAT | O_RDWR | O_NOFOLLOW | O_CLOEXEC, S_IRUSR | S_IWUSR)
        guard lock >= 0 else {
            throw InstallError.message("The installer can’t open its lock file (\(String(cString: strerror(errno)))):\n\(lockURL.path)\n\nCheck that your user account can write to your VST3 folder, then try again.")
        }
        var info = stat()
        guard fstat(lock, &info) == 0, (info.st_mode & S_IFMT) == S_IFREG, info.st_nlink == 1, info.st_uid == geteuid() else {
            close(lock)
            throw InstallError.message("The installation lock file is not a normal file owned by you, so the installer stopped for safety:\n\(lockURL.path)\n\nMove that file to the Trash, then try again.")
        }
        guard flock(lock, LOCK_EX | LOCK_NB) == 0 else {
            close(lock)
            throw InstallError.message("Another Vivid Fever Dreams installer is changing your VST3 folder right now. Let it finish (or quit it), then try again.")
        }
        return lock
    }

    /// Moves a previous copy that could not be restored out of the VST3 folder, under a visible name.
    private func preservePrevious(_ stage: URL, version: String?) -> URL {
        let formatter = DateFormatter()
        formatter.locale = Locale(identifier: "en_US_POSIX")
        formatter.dateFormat = "yyyy-MM-dd HH.mm.ss"
        let name = "ENERGY HARVESTER \(version ?? "previous") (saved \(formatter.string(from: Date()))).vst3"
        do {
            try fm.createDirectory(at: recoveryRoot, withIntermediateDirectories: true)
            try Self.refuseSymlinkAncestors(recoveryRoot)
            let target = recoveryRoot.appendingPathComponent(name)
            guard renameatx_np(AT_FDCWD, stage.path, AT_FDCWD, target.path, UInt32(RENAME_EXCL)) == 0 else { return stage }
            log.append("Previous plug-in saved to \(target.path).")
            return target
        } catch {
            return stage
        }
    }

    func install() throws -> InstallResult {
        log = ["ENERGY HARVESTER installer \(manifest.version)", "Destination: \(destination.path)"]
        try checkHosts()
        try Self.refuseSymlinkAncestors(destination)
        guard destination.lastPathComponent == "ENERGY HARVESTER.vst3" else {
            throw InstallError.message("The destination must be named ENERGY HARVESTER.vst3.")
        }
        guard !Self.exists(systemCopy) else {
            throw InstallError.message("ENERGY HARVESTER is already in the system VST3 folder:\n\(systemCopy.path)\n\nTo avoid duplicate entries, move that ENERGY HARVESTER.vst3 into your user VST3 folder first, then run this installer again:\n\(destination.deletingLastPathComponent().path)")
        }
        try verifyPayload(at: payload)
        log.append("Embedded payload: SHA-256, bundle identity, VST3 identities and code signature verified.")
        let parent = destination.deletingLastPathComponent()
        try fm.createDirectory(at: parent, withIntermediateDirectories: true)
        try Self.refuseSymlinkAncestors(parent)
        let lock = try Self.acquireSuiteLock(in: parent)
        defer { flock(lock, LOCK_UN); close(lock) }
        if let pending = try fm.contentsOfDirectory(atPath: parent.path).first(where: { $0.hasPrefix(Self.suiteTransactionPrefix) }) {
            throw InstallError.message("An interrupted Vivid Fever Dreams Suite installation needs attention first, so ENERGY HARVESTER was not changed. Open the Vivid Fever Dreams Suite installer and follow its directions.\n\nHidden folder: \(parent.appendingPathComponent(pending).path)")
        }
        let updated = Self.exists(destination)
        var previousIdentity: PluginIdentity?
        var previousHashes: [String: String]?
        if updated {
            let oldIdentity = try Self.identity(at: destination)
            guard oldIdentity.componentID == manifest.componentID,
                  oldIdentity.controllerID == manifest.controllerID else {
                throw InstallError.message("This ENERGY HARVESTER copy uses different VST3 identities. It has been left untouched to protect existing projects.")
            }
            let oldVersion = oldIdentity.version
            guard oldVersion.compare(manifest.version, options: .numeric) != .orderedDescending else {
                throw InstallError.message("ENERGY HARVESTER \(oldVersion) is already installed. This installer contains the older version \(manifest.version), so no changes were made.")
            }
            // Refuse links anywhere in an installed bundle before replacing it.
            previousIdentity = oldIdentity
            previousHashes = try Self.fileHashes(in: destination)
            log.append("Updating existing version \(oldVersion).")
        }
        // No .vst3 extension: audio apps scanning the VST3 folder never see the temporary copy.
        let stage = parent.appendingPathComponent(".energyharvester-stage-\(UUID().uuidString)", isDirectory: true)
        var ownsStage = false
        defer {
            if ownsStage && Self.exists(stage) {
                do { try fm.removeItem(at: stage) }
                catch { log.append("Temporary transaction folder remains at \(stage.path): \(error.localizedDescription)") }
            }
        }
        try fm.copyItem(at: payload, to: stage)
        ownsStage = true
        try Self.stripQuarantine(in: stage)
        try verifyPayload(at: stage)
        try afterStagingForTesting?()
        try checkHosts()
        try Self.refuseSymlinkAncestors(destination)
        guard Self.exists(destination) == updated else {
            throw InstallError.message("The installed plug-in changed during preparation. Please try again.")
        }
        if updated {
            let current = try Self.identity(at: destination)
            guard let originalIdentity = previousIdentity,
                  let originalHashes = previousHashes,
                  current == originalIdentity,
                  try Self.fileHashes(in: destination) == originalHashes else {
                throw InstallError.message("The installed plug-in changed during preparation. Please try again.")
            }
        }
        let flags = updated ? UInt32(RENAME_SWAP) : UInt32(RENAME_EXCL)
        guard renameatx_np(AT_FDCWD, stage.path, AT_FDCWD, destination.path, flags) == 0 else {
            throw InstallError.message("Unable to replace ENERGY HARVESTER: \(String(cString: strerror(errno))). The previous version remains in place.")
        }
        do {
            try afterCommitForTesting?()
            if failAfterCommitForTesting { throw InstallError.message("Injected verification failure") }
            try verifyPayload(at: destination)
            log.append(updated ? "Atomic replacement completed and verified." : "Installation completed and verified.")
        } catch {
            // Undo only our own commit: the destination must still be exactly the bundle this installer placed there.
            let destinationIsOurs = (try? Self.fileHashes(in: destination)) == manifest.files
            if updated {
                guard destinationIsOurs,
                      renameatx_np(AT_FDCWD, stage.path, AT_FDCWD, destination.path, UInt32(RENAME_SWAP)) == 0 else {
                    ownsStage = false
                    let saved = preservePrevious(stage, version: previousIdentity?.version)
                    throw InstallError.message("Verification failed and the automatic rollback could not finish, so the plug-in in your VST3 folder was left as it is. Your previous ENERGY HARVESTER was saved to:\n\(saved.path)\n\nTo restore it, quit your audio apps, move ENERGY HARVESTER.vst3 out of ~/Library/Audio/Plug-Ins/VST3, then put the saved copy there and rename it ENERGY HARVESTER.vst3. Details: \(error.localizedDescription)")
                }
                log.append("Verification failed; previous plug-in restored atomically.")
            } else {
                guard destinationIsOurs else {
                    throw InstallError.message("Verification failed, and \(destination.path) changed while the installer was working, so it was left in place. Details: \(error.localizedDescription)")
                }
                guard renameatx_np(AT_FDCWD, destination.path, AT_FDCWD, stage.path, UInt32(RENAME_EXCL)) == 0 else {
                    throw InstallError.message("Verification failed. Could not remove the new plug-in at \(destination.path): \(error.localizedDescription)")
                }
                log.append("Verification failed; initial installation rolled back.")
            }
            throw error
        }
        log.append("Saved presets, DAW projects and other plug-ins were not modified.")
        return InstallResult(updated: updated, version: manifest.version, destination: destination, log: log)
    }
}
