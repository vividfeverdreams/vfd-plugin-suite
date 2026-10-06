import Foundation
import CryptoKit
import Darwin

enum InstallError: LocalizedError {
    case message(String)
    var errorDescription: String? { if case .message(let value) = self { return value }; return nil }
}

enum PluginFormat: String, Codable, CaseIterable, Comparable {
    case vst3, au
    var fileExtension: String { self == .vst3 ? "vst3" : "component" }
    var label: String { self == .vst3 ? "VST3" : "AU" }
    static func < (lhs: PluginFormat, rhs: PluginFormat) -> Bool { lhs == .vst3 && rhs == .au }
}

/// The identity macOS registers an Audio Unit under: the AudioComponents type, subtype and manufacturer codes.
struct AudioUnitIdentity: Codable, Hashable {
    let type: String
    let subtype: String
    let manufacturer: String
    var description: String { "\(type)/\(subtype)/\(manufacturer)" }
}

/// Format 2 covers the VST3 and the AU bundle of the same release.
struct PayloadManifest: Codable {
    let format: Int
    let version: String
    let bundleIdentifier: String
    let componentID: String
    let controllerID: String
    let audioUnits: [AudioUnitIdentity]
    let vst3Files: [String: String]
    let audioUnitFiles: [String: String]
    func files(_ format: PluginFormat) -> [String: String] { format == .vst3 ? vst3Files : audioUnitFiles }
}

struct InstallResult {
    let installed: [PluginFormat]
    let updated: [PluginFormat]
    /// Selected formats whose installed bytes already matched the payload; they were not touched.
    let skipped: [PluginFormat]
    let version: String
    let audioUnitsRefreshed: Bool
    let log: [String]
    var summary: String {
        let parts = installed.map { "\($0.label) installed" } + updated.map { "\($0.label) updated" } + skipped.map { "\($0.label) already up to date" }
        return parts.joined(separator: ", ")
    }
}

struct PluginIdentity: Equatable {
    let version: String
    var componentID: String?
    var controllerID: String?
    var audioUnits: [AudioUnitIdentity]?
}

/// Where everything lives. The shipping app uses `standard`; every self-test uses a temporary folder.
struct InstallLocations {
    var userVST3: URL
    var userComponents: URL
    var systemVST3: URL
    var systemComponents: URL
    var recovered: URL
    func destinationRoot(_ format: PluginFormat) -> URL { format == .vst3 ? userVST3 : userComponents }
    func systemRoot(_ format: PluginFormat) -> URL { format == .vst3 ? systemVST3 : systemComponents }
    static var standard: InstallLocations {
        let home = FileManager.default.homeDirectoryForCurrentUser
        return InstallLocations(userVST3: home.appendingPathComponent("Library/Audio/Plug-Ins/VST3"),
                                userComponents: home.appendingPathComponent("Library/Audio/Plug-Ins/Components"),
                                systemVST3: URL(fileURLWithPath: "/Library/Audio/Plug-Ins/VST3"),
                                systemComponents: URL(fileURLWithPath: "/Library/Audio/Plug-Ins/Components"),
                                recovered: home.appendingPathComponent("Library/Application Support/Vivid Fever Dreams/Recovered"))
    }
    static func inside(_ folder: URL) -> InstallLocations {
        InstallLocations(userVST3: folder.appendingPathComponent("User/Library/Audio/Plug-Ins/VST3"),
                         userComponents: folder.appendingPathComponent("User/Library/Audio/Plug-Ins/Components"),
                         systemVST3: folder.appendingPathComponent("System/Library/Audio/Plug-Ins/VST3"),
                         systemComponents: folder.appendingPathComponent("System/Library/Audio/Plug-Ins/Components"),
                         recovered: folder.appendingPathComponent("User/Library/Application Support/Vivid Fever Dreams/Recovered"))
    }
}

/// Command-line modes. Anything without a recognised "--" option opens the window, because LaunchServices can
/// append arguments such as -psn_0_NNNN on a Gatekeeper-approved first launch.
enum LaunchMode: Equatable {
    case gui, selfTest, inspectPayload, checkHosts
    case makeManifest(payload: String, manifest: String, version: String)
    case checkWhatsNew(String)
    case invalid
    static func parse(_ rawArguments: [String]) -> LaunchMode {
        let arguments = Array(rawArguments.dropFirst().filter { !$0.hasPrefix("-psn_") })
        guard let flag = arguments.first(where: { $0.hasPrefix("--") }) else { return .gui }
        guard arguments.first == flag else { return .invalid }
        switch (flag, arguments.count) {
        case ("--self-test", 1): return .selfTest
        case ("--inspect-payload", 1): return .inspectPayload
        case ("--check-hosts", 1): return .checkHosts
        case ("--check-whats-new", 2): return .checkWhatsNew(arguments[1])
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
    /// VST3-capable hosts with stable bundle identifiers (compared lowercased). Most of them load Audio Units too.
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
    /// Hosts that only load Audio Units: they must be quit only when the AU is being installed.
    static let audioUnitHostIdentifiers: Set<String> = ["com.apple.logic10", "com.apple.garageband10", "com.apple.mainstage3"]
    static let audioUnitHostNames = ["logic pro", "garageband", "mainstage"]

    private static func nameMatches(_ applicationName: String, _ names: [String]) -> Bool {
        names.contains { hostName in
            if applicationName == hostName { return true }
            guard applicationName.hasPrefix(hostName + " ") else { return false }
            return applicationName.dropFirst(hostName.count + 1).first?.isNumber == true
        }
    }
    static func isAudioApplication(bundleIdentifier: String?, name: String?, processID: Int32, installerProcessID: Int32,
                                   formats: Set<PluginFormat> = [.vst3]) -> Bool {
        guard processID != installerProcessID, !isInstaller(bundleIdentifier) else { return false }
        let id = (bundleIdentifier ?? "").lowercased()
        let applicationName = (name ?? "").lowercased()
        if hostIdentifiers.contains(id) || hostIdentifierPrefixes.contains(where: id.hasPrefix) { return true }
        if nameMatches(applicationName, hostNames) { return true }
        guard formats.contains(.au) else { return false }
        return audioUnitHostIdentifiers.contains(id) || nameMatches(applicationName, audioUnitHostNames)
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

/// The engine receives explicit locations so its entire transaction can be exercised in a temporary directory.
/// The shipping UI always supplies the standard per-user plug-in folders.
final class InstallerEngine {
    static let identifier = "com.yoiboi.throwup"
    static let executable = "THROW-UP"
    /// JUCE derives these from IS_SYNTH, PLUGIN_CODE and PLUGIN_MANUFACTURER_CODE in the plug-in's CMakeLists.txt.
    static let audioUnit = AudioUnitIdentity(type: "aumf", subtype: "Thr1", manufacturer: "Yoib") // MIDI-controlled effect (MIDI throws in Logic)
    /// Shared with the Vivid Fever Dreams Suite installer, so the two never change the plug-in folders at the same time.
    static let suiteLockName = ".vfd-suite-installer.lock"
    static let suiteTransactionPrefix = ".vfd-suite-transaction-"
    /// Build-time guard for --make-manifest: the released VST3 class IDs (nil when not pinned).
    static let pinnedIdentity: (componentID: String, controllerID: String)? = nil
    private let fm = FileManager.default
    /// Resources/Payload, holding THROW-UP.vst3 and THROW-UP.component.
    let payloadRoot: URL
    let manifest: PayloadManifest
    let locations: InstallLocations
    let runningHosts: (Set<PluginFormat>) -> [String]
    var log: [String] = []
    /// Makes macOS re-read its Audio Unit registrations. Replaced by a recorder in the self-tests.
    var refreshAudioUnits: () -> String = { InstallerEngine.refreshAudioUnitRegistrar() }
    // Test fault injection never appears in the app's command line or UI.
    var failAfterCommitForTesting = false
    var afterStagingForTesting: (() throws -> Void)?
    var afterCommitForTesting: ((PluginFormat) throws -> Void)?

    init(payloadRoot: URL, manifest: PayloadManifest, locations: InstallLocations,
         runningHosts: @escaping (Set<PluginFormat>) -> [String]) {
        // The payload is only read, so it may be reached through a symlinked folder.
        self.payloadRoot = Self.canonical(payloadRoot)
        self.manifest = manifest
        self.locations = locations
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

    static func isRealDirectory(_ url: URL) -> Bool {
        var info = stat()
        return lstat(url.path, &info) == 0 && (info.st_mode & S_IFMT) == S_IFDIR
    }

    func payloadURL(_ format: PluginFormat) -> URL { payloadRoot.appendingPathComponent("THROW-UP.\(format.fileExtension)") }
    func destinationURL(_ format: PluginFormat) -> URL { locations.destinationRoot(format).appendingPathComponent("THROW-UP.\(format.fileExtension)") }
    func systemURL(_ format: PluginFormat) -> URL { locations.systemRoot(format).appendingPathComponent("THROW-UP.\(format.fileExtension)") }

    static func refuseSymlinkAncestors(_ url: URL) throws {
        var current = URL(fileURLWithPath: "/", isDirectory: true)
        for component in url.pathComponents.dropFirst() {
            current.appendPathComponent(component)
            var info = stat()
            if lstat(current.path, &info) == 0 {
                guard (info.st_mode & S_IFMT) != S_IFLNK else {
                    throw InstallError.message("Installation stopped because this path is a symbolic link: \(current.path). Use a normal folder for THROW-UP.")
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
            throw InstallError.message("The THROW-UP payload cannot be read.")
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
        guard !hashes.isEmpty else { throw InstallError.message("The THROW-UP payload is empty.") }
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
            throw InstallError.message("The THROW-UP payload cannot be read.")
        }
        for case let relative as String in enumerator { try strip(root.path + "/" + relative) }
    }

    /// macOS caches Audio Unit registrations in the per-user AudioComponentRegistrar agent. Ending it — the approach
    /// plug-in installers commonly use — makes macOS rebuild that cache the next time an app asks; launchd starts it
    /// again on demand. Apple does not document this, so it is best effort: SIGTERM, current user's process only.
    static func refreshAudioUnitRegistrar() -> String {
        let process = Process()
        process.executableURL = URL(fileURLWithPath: "/usr/bin/killall")
        process.arguments = ["-u", NSUserName(), "AudioComponentRegistrar"]
        process.standardOutput = FileHandle.nullDevice
        process.standardError = FileHandle.nullDevice
        do { try process.run(); process.waitUntilExit() }
        catch { return "macOS will refresh its Audio Unit list after a restart (\(error.localizedDescription))." }
        return process.terminationStatus == 0 ? "Asked macOS to refresh its Audio Unit list." :
            "macOS will refresh its Audio Unit list the next time an app scans for Audio Units."
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

    static func identity(at bundle: URL, format: PluginFormat = .vst3) throws -> PluginIdentity {
        try refuseSymlinkAncestors(bundle)
        let plist = bundle.appendingPathComponent("Contents/Info.plist")
        let object = try PropertyListSerialization.propertyList(from: Data(contentsOf: plist), format: nil)
        guard let info = object as? [String: Any],
              info["CFBundleIdentifier"] as? String == identifier,
              info["CFBundleExecutable"] as? String == executable,
              let version = info["CFBundleShortVersionString"] as? String else {
            throw InstallError.message("The existing item is not the expected THROW-UP plug-in. It has been left untouched.")
        }
        if format == .au {
            guard let components = info["AudioComponents"] as? [[String: Any]], !components.isEmpty else {
                throw InstallError.message("The Audio Unit identity does not match THROW-UP. No installed plug-in was changed.")
            }
            var units: [AudioUnitIdentity] = []
            for component in components {
                guard let type = component["type"] as? String, let subtype = component["subtype"] as? String,
                      let manufacturer = component["manufacturer"] as? String,
                      [type, subtype, manufacturer].allSatisfy({ $0.utf8.count == 4 }) else {
                    throw InstallError.message("The Audio Unit identity does not match THROW-UP. No installed plug-in was changed.")
                }
                units.append(AudioUnitIdentity(type: type, subtype: subtype, manufacturer: manufacturer))
            }
            return PluginIdentity(version: version, audioUnits: units)
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
            throw InstallError.message("The VST3 identity does not match THROW-UP. No installed plug-in was changed.")
        }
        return PluginIdentity(version: version, componentID: component.uppercased(), controllerID: controller.uppercased())
    }

    /// True when an installed bundle is the same plug-in as this release (VST3 class IDs, or the AU's registered codes).
    func sameProduct(_ identity: PluginIdentity, format: PluginFormat) -> Bool {
        if format == .vst3 { return identity.componentID == manifest.componentID && identity.controllerID == manifest.controllerID }
        return identity.audioUnits?.contains(Self.audioUnit) == true
    }

    func verifyPayload(at bundle: URL, format: PluginFormat) throws {
        guard manifest.format == 2, manifest.bundleIdentifier == Self.identifier, manifest.audioUnits.contains(Self.audioUnit) else {
            throw InstallError.message("The installer manifest does not describe this THROW-UP plug-in.")
        }
        let identity = try Self.identity(at: bundle, format: format)
        guard identity.version == manifest.version, sameProduct(identity, format: format),
              format == .vst3 || identity.audioUnits == manifest.audioUnits else {
            throw InstallError.message("The plug-in identity does not match the installer.")
        }
        guard fm.isExecutableFile(atPath: bundle.appendingPathComponent("Contents/MacOS/THROW-UP").path) else {
            throw InstallError.message("The plug-in executable does not have executable permissions.")
        }
        guard try Self.fileHashes(in: bundle) == manifest.files(format) else {
            throw InstallError.message("The plug-in files did not pass their integrity check. Download a fresh installer and try again.")
        }
        try Self.run("/usr/bin/codesign", ["--verify", "--deep", "--strict", bundle.path])
    }

    private func checkHosts(_ formats: Set<PluginFormat>) throws {
        let hosts = runningHosts(formats).sorted()
        guard hosts.isEmpty else {
            throw InstallError.message("Quit \(hosts.joined(separator: ", ")) and click Install / Update again. Your current plug-in has not been changed.")
        }
    }

    /// The suite installer's lock file, so a single-plugin update and a suite transaction never overlap.
    static func acquireSuiteLock(in folder: URL) throws -> Int32 {
        let lockURL = folder.appendingPathComponent(suiteLockName)
        let lock = open(lockURL.path, O_CREAT | O_RDWR | O_NOFOLLOW | O_CLOEXEC, S_IRUSR | S_IWUSR)
        guard lock >= 0 else {
            throw InstallError.message("The installer can’t open its lock file (\(String(cString: strerror(errno)))):\n\(lockURL.path)\n\nCheck that your user account can write to your plug-in folder, then try again.")
        }
        var info = stat()
        guard fstat(lock, &info) == 0, (info.st_mode & S_IFMT) == S_IFREG, info.st_nlink == 1, info.st_uid == geteuid() else {
            close(lock)
            throw InstallError.message("The installation lock file is not a normal file owned by you, so the installer stopped for safety:\n\(lockURL.path)\n\nMove that file to the Trash, then try again.")
        }
        guard flock(lock, LOCK_EX | LOCK_NB) == 0 else {
            close(lock)
            throw InstallError.message("Another Vivid Fever Dreams installer is changing your plug-in folders right now. Let it finish (or quit it), then try again.")
        }
        return lock
    }

    /// Moves a previous copy that could not be restored out of the plug-in folders, under a visible name.
    private func preservePrevious(_ stage: URL, format: PluginFormat, version: String?) -> URL {
        let formatter = DateFormatter()
        formatter.locale = Locale(identifier: "en_US_POSIX")
        formatter.dateFormat = "yyyy-MM-dd HH.mm.ss"
        let name = "THROW-UP \(version ?? "previous") (saved \(formatter.string(from: Date()))).\(format.fileExtension)"
        do {
            try fm.createDirectory(at: locations.recovered, withIntermediateDirectories: true)
            try Self.refuseSymlinkAncestors(locations.recovered)
            let target = locations.recovered.appendingPathComponent(name)
            guard renameatx_np(AT_FDCWD, stage.path, AT_FDCWD, target.path, UInt32(RENAME_EXCL)) == 0 else { return stage }
            log.append("Previous plug-in saved to \(target.path).")
            return target
        } catch {
            return stage
        }
    }

    private struct Item {
        let format: PluginFormat
        let destination: URL
        var stage: URL
        let updated: Bool
        let previousIdentity: PluginIdentity?
        let previousHashes: [String: String]?
    }

    func install(formats selected: Set<PluginFormat> = [.vst3]) throws -> InstallResult {
        let formats = selected.sorted()
        guard !formats.isEmpty else { throw InstallError.message("Tick VST3, AU or both.") }
        log = ["THROW-UP installer \(manifest.version)", "Formats: \(formats.map(\.label).joined(separator: ", "))"]
        try checkHosts(selected)
        for format in formats {
            let destination = destinationURL(format)
            try Self.refuseSymlinkAncestors(destination)
            guard !Self.exists(systemURL(format)) else {
                throw InstallError.message("THROW-UP is already in the system \(format.label) folder:\n\(systemURL(format).path)\n\nTo avoid duplicate entries, move that copy into your user folder first, then run this installer again:\n\(destination.deletingLastPathComponent().path)")
            }
            try verifyPayload(at: payloadURL(format), format: format)
        }
        log.append("Embedded payload: SHA-256, bundle identity, plug-in identities and code signature verified.")
        var locks: [Int32] = []
        defer { for lock in locks { flock(lock, LOCK_UN); close(lock) } }
        for format in PluginFormat.allCases.sorted() {
            let parent = locations.destinationRoot(format)
            try Self.refuseSymlinkAncestors(parent)
            if selected.contains(format) {
                try fm.createDirectory(at: parent, withIntermediateDirectories: true)
                try Self.refuseSymlinkAncestors(parent)
            } else if !Self.isRealDirectory(parent) { continue }
            locks.append(try Self.acquireSuiteLock(in: parent))
            if let pending = try fm.contentsOfDirectory(atPath: parent.path).first(where: { $0.hasPrefix(Self.suiteTransactionPrefix) }) {
                throw InstallError.message("An interrupted Vivid Fever Dreams Suite installation needs attention first, so THROW-UP was not changed. Open the Vivid Fever Dreams Suite installer and click Repair….\n\nHidden folder: \(parent.appendingPathComponent(pending).path)")
            }
        }
        var work: [Item] = []
        var skipped: [PluginFormat] = []
        for format in formats {
            let destination = destinationURL(format)
            guard Self.exists(destination) else {
                work.append(Item(format: format, destination: destination, stage: destination, updated: false, previousIdentity: nil, previousHashes: nil))
                continue
            }
            let oldIdentity = try Self.identity(at: destination, format: format)
            guard sameProduct(oldIdentity, format: format) else {
                let what = format == .vst3 ? "VST3 identities" : "Audio Unit codes"
                throw InstallError.message("This THROW-UP \(format.label) copy uses different \(what). It has been left untouched to protect existing projects.")
            }
            guard oldIdentity.version.compare(manifest.version, options: .numeric) != .orderedDescending else {
                throw InstallError.message("THROW-UP \(format.label) \(oldIdentity.version) is already installed. This installer contains the older version \(manifest.version), so no changes were made.")
            }
            // Refuse links anywhere in an installed bundle before replacing it.
            let hashes = try Self.fileHashes(in: destination)
            if hashes == manifest.files(format) {
                skipped.append(format)
                log.append("\(format.label) is already up to date; left untouched.")
                continue
            }
            log.append("Updating existing \(format.label) version \(oldIdentity.version).")
            work.append(Item(format: format, destination: destination, stage: destination, updated: true, previousIdentity: oldIdentity, previousHashes: hashes))
        }
        guard !work.isEmpty else {
            return InstallResult(installed: [], updated: [], skipped: skipped, version: manifest.version, audioUnitsRefreshed: false, log: log)
        }
        var owned: [URL] = []
        var preserved: Set<String> = []
        defer {
            for stage in owned where !preserved.contains(stage.path) && Self.exists(stage) {
                do { try fm.removeItem(at: stage) }
                catch { log.append("Temporary transaction folder remains at \(stage.path): \(error.localizedDescription)") }
            }
        }
        for index in work.indices {
            // No .vst3 or .component extension: audio apps and the AU registrar never see the temporary copy.
            let stage = locations.destinationRoot(work[index].format).appendingPathComponent(".throwup-stage-\(UUID().uuidString)", isDirectory: true)
            try fm.copyItem(at: payloadURL(work[index].format), to: stage)
            owned.append(stage)
            work[index].stage = stage
            try Self.stripQuarantine(in: stage)
            try verifyPayload(at: stage, format: work[index].format)
        }
        try afterStagingForTesting?()
        try checkHosts(selected)
        for item in work {
            try Self.refuseSymlinkAncestors(item.destination)
            guard Self.exists(item.destination) == item.updated else {
                throw InstallError.message("The installed plug-in changed during preparation. Please try again.")
            }
            if item.updated {
                let current = try Self.identity(at: item.destination, format: item.format)
                guard current == item.previousIdentity, try Self.fileHashes(in: item.destination) == item.previousHashes else {
                    throw InstallError.message("The installed plug-in changed during preparation. Please try again.")
                }
            }
        }
        var committed: [Int] = []
        do {
            for (index, item) in work.enumerated() {
                let flags = item.updated ? UInt32(RENAME_SWAP) : UInt32(RENAME_EXCL)
                guard renameatx_np(AT_FDCWD, item.stage.path, AT_FDCWD, item.destination.path, flags) == 0 else {
                    throw InstallError.message("Unable to replace THROW-UP \(item.format.label): \(String(cString: strerror(errno))).")
                }
                committed.append(index)
                try afterCommitForTesting?(item.format)
            }
            if failAfterCommitForTesting { throw InstallError.message("Injected verification failure") }
            for item in work { try verifyPayload(at: item.destination, format: item.format) }
            log.append("Installation completed and verified: \(work.map(\.format.label).joined(separator: ", ")).")
        } catch {
            // Undo only our own commits: each destination must still be exactly the bundle this installer placed there.
            var problems: [String] = []
            for index in committed.reversed() {
                let item = work[index]
                let destinationIsOurs = (try? Self.fileHashes(in: item.destination)) == manifest.files(item.format)
                if item.updated {
                    if destinationIsOurs && renameatx_np(AT_FDCWD, item.stage.path, AT_FDCWD, item.destination.path, UInt32(RENAME_SWAP)) == 0 {
                        log.append("Verification failed; previous \(item.format.label) restored atomically.")
                        continue
                    }
                    preserved.insert(item.stage.path)
                    let saved = preservePrevious(item.stage, format: item.format, version: item.previousIdentity?.version)
                    problems.append("The \(item.format.label) in your plug-in folder was left as it is. Your previous THROW-UP \(item.format.label) was saved to:\n\(saved.path)\nTo restore it, quit your audio apps and use Repair… in the Vivid Fever Dreams Suite installer, or replace THROW-UP.\(item.format.fileExtension) with the saved copy.")
                } else {
                    guard destinationIsOurs else {
                        problems.append("\(item.destination.path) changed while the installer was working, so it was left in place.")
                        continue
                    }
                    if renameatx_np(AT_FDCWD, item.destination.path, AT_FDCWD, item.stage.path, UInt32(RENAME_EXCL)) == 0 {
                        log.append("Verification failed; new \(item.format.label) installation rolled back.")
                    } else {
                        problems.append("Could not remove the new plug-in at \(item.destination.path).")
                    }
                }
            }
            if !problems.isEmpty {
                throw InstallError.message("Verification failed and the automatic rollback could not finish.\n\n\(problems.joined(separator: "\n\n"))\n\nDetails: \(error.localizedDescription)")
            }
            throw error
        }
        var refreshed = false
        if work.contains(where: { $0.format == .au }) {
            log.append(refreshAudioUnits())
            refreshed = true
        }
        log.append("Saved presets, DAW projects and other plug-ins were not modified.")
        return InstallResult(installed: work.filter { !$0.updated }.map(\.format), updated: work.filter(\.updated).map(\.format),
                             skipped: skipped, version: manifest.version, audioUnitsRefreshed: refreshed, log: log)
    }
}
