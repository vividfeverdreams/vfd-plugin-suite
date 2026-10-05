import Foundation
import CryptoKit
import Darwin

enum InstallError: LocalizedError {
    case message(String)
    /// A plain-English summary for the window, plus the raw technical detail that is kept for the log.
    case technical(String, detail: String)
    var errorDescription: String? {
        switch self {
        case .message(let value): return value
        case .technical(let value, _): return value
        }
    }
}

/// Turns errors into text a non-technical user can act on. The raw text still goes to the log.
enum UserFacingText {
    static func message(for error: Error) -> String {
        if let installError = error as? InstallError { return installError.errorDescription ?? "The installation did not complete." }
        let nsError = error as NSError
        if isOutOfSpace(nsError) {
            return "There isn’t enough free disk space to prepare the suite. Free up at least 500 MB, then try again. Your installed plug-ins were left as they were."
        }
        if isPermissionDenied(nsError) {
            return "macOS did not allow the installer to change a file it needs\(pathSuffix(nsError)). Make sure your user account can write to ~/Library/Audio/Plug-Ins, then try again."
        }
        if nsError.domain == NSCocoaErrorDomain && [NSFileNoSuchFileError, NSFileReadNoSuchFileError].contains(nsError.code) {
            return "A file the installer needed was missing\(pathSuffix(nsError)). If this keeps happening, download a fresh copy of the installer."
        }
        return error.localizedDescription
    }
    static func logText(for error: Error) -> String {
        if let installError = error as? InstallError {
            if case .technical(let summary, let detail) = installError { return summary + "\nDetails: " + detail }
            return installError.errorDescription ?? ""
        }
        let nsError = error as NSError
        var text = message(for: error) + "\nDetails: \(nsError.domain) \(nsError.code): \(nsError.localizedDescription)"
        if let underlying = nsError.userInfo[NSUnderlyingErrorKey] as? NSError {
            text += " (underlying \(underlying.domain) \(underlying.code): \(underlying.localizedDescription))"
        }
        return text
    }
    private static func posixCode(_ error: NSError) -> Int32? {
        if error.domain == NSPOSIXErrorDomain { return Int32(truncatingIfNeeded: error.code) }
        if let underlying = error.userInfo[NSUnderlyingErrorKey] as? NSError { return posixCode(underlying) }
        return nil
    }
    private static func isOutOfSpace(_ error: NSError) -> Bool {
        (error.domain == NSCocoaErrorDomain && error.code == NSFileWriteOutOfSpaceError) ||
            posixCode(error) == ENOSPC || posixCode(error) == EDQUOT
    }
    private static func isPermissionDenied(_ error: NSError) -> Bool {
        if error.domain == NSCocoaErrorDomain && [NSFileWriteNoPermissionError, NSFileReadNoPermissionError].contains(error.code) { return true }
        guard let code = posixCode(error) else { return false }
        return code == EACCES || code == EPERM
    }
    private static func pathSuffix(_ error: NSError) -> String {
        if let path = error.userInfo[NSFilePathErrorKey] as? String { return " (\(path))" }
        if let url = error.userInfo[NSURLErrorKey] as? URL { return " (\(url.path))" }
        return ""
    }
}

/// Dotted numeric versions compared component by component; missing components count as zero, so 1.0 == 1.0.0.
enum VersionOrder {
    static func compare(_ lhs: String, _ rhs: String) -> ComparisonResult {
        let left = lhs.split(separator: ".", omittingEmptySubsequences: false)
        let right = rhs.split(separator: ".", omittingEmptySubsequences: false)
        for index in 0..<max(left.count, right.count) {
            // Compare digit strings without leading zeros: shorter is smaller, equal lengths compare lexically.
            let a = index < left.count ? left[index].drop { $0 == "0" } : ""
            let b = index < right.count ? right[index].drop { $0 == "0" } : ""
            if a.count != b.count { return a.count < b.count ? .orderedAscending : .orderedDescending }
            if a != b { return a < b ? .orderedAscending : .orderedDescending }
        }
        return .orderedSame
    }
}

/// Command-line modes. Anything without a recognised "--" option launches the window, because LaunchServices can
/// append arguments such as -psn_0_NNNN (Gatekeeper-approved first launch) or -NSDocumentRevisionsDebugMode.
enum LaunchMode: Equatable {
    case gui, selfTest, inspectPayload, checkHosts
    case makeManifest(payload: String, manifest: String, version: String)
    case checkWhatsNew(String)
    case renderPreview(String, state: String)
    case invalid
    static let previewStates = ["main", "uninstall", "result", "repair"]
    static func parse(_ rawArguments: [String]) -> LaunchMode {
        let arguments = Array(rawArguments.dropFirst().filter { !$0.hasPrefix("-psn_") })
        guard let flag = arguments.first(where: { $0.hasPrefix("--") }) else { return .gui }
        guard arguments.first == flag else { return .invalid }
        switch (flag, arguments.count) {
        case ("--self-test", 1): return .selfTest
        case ("--inspect-payload", 1): return .inspectPayload
        case ("--check-hosts", 1): return .checkHosts
        case ("--check-whats-new", 2): return .checkWhatsNew(arguments[1])
        case ("--render-preview", 2): return .renderPreview(arguments[1], state: "main")
        case ("--render-preview", 3) where previewStates.contains(arguments[2]): return .renderPreview(arguments[1], state: arguments[2])
        case ("--make-manifest", 4): return .makeManifest(payload: arguments[1], manifest: arguments[2], version: arguments[3])
        default: return .invalid
        }
    }
}

// MARK: Catalog

enum PluginFormat: String, Codable, CaseIterable, Comparable {
    case vst3, au
    var fileExtension: String { self == .vst3 ? "vst3" : "component" }
    var label: String { self == .vst3 ? "VST3" : "AU" }
    var folderDescription: String { self == .vst3 ? "~/Library/Audio/Plug-Ins/VST3" : "~/Library/Audio/Plug-Ins/Components" }
    static func < (lhs: PluginFormat, rhs: PluginFormat) -> Bool { lhs == .vst3 && rhs == .au }
}

/// The identity macOS registers an Audio Unit under: the AudioComponents type, subtype and manufacturer codes.
struct AudioUnitIdentity: Codable, Hashable {
    let type: String
    let subtype: String
    let manufacturer: String
    var description: String { "\(type)/\(subtype)/\(manufacturer)" }
}

struct PluginDefinition {
    let name: String
    let bundleIdentifier: String
    let version: String
    /// JUCE derives these from IS_SYNTH, PLUGIN_CODE and PLUGIN_MANUFACTURER_CODE in each plug-in's CMakeLists.txt.
    let audioUnit: AudioUnitIdentity
    func filename(for format: PluginFormat) -> String { "\(name).\(format.fileExtension)" }
}

/// One installable bundle: a plug-in in one format.
struct ItemKey: Hashable, Codable, Comparable {
    let plugin: Int
    let format: PluginFormat
    var definition: PluginDefinition { SuiteCatalog.plugins[plugin] }
    var filename: String { definition.filename(for: format) }
    var title: String { "\(definition.name) \(format.label)" }
    static func < (lhs: ItemKey, rhs: ItemKey) -> Bool { (lhs.plugin, lhs.format == .au ? 1 : 0) < (rhs.plugin, rhs.format == .au ? 1 : 0) }
}

enum SuiteCatalog {
    static let installerIdentifier = "xyz.vividfeverdreams.suite.installer"
    static let plugins = [
        PluginDefinition(name: "YOI-BOI", bundleIdentifier: "com.yoiboi.synth", version: "0.2.0",
                         audioUnit: AudioUnitIdentity(type: "aumu", subtype: "YB01", manufacturer: "YBoi")),
        PluginDefinition(name: "THROW-UP", bundleIdentifier: "com.yoiboi.throwup", version: "0.2.0",
                         audioUnit: AudioUnitIdentity(type: "aufx", subtype: "Thr1", manufacturer: "Yoib")),
        PluginDefinition(name: "BACK-SCRATCH", bundleIdentifier: "com.yoiboi.backscratch", version: "0.11.0",
                         audioUnit: AudioUnitIdentity(type: "aufx", subtype: "Bsc1", manufacturer: "Yoib")),
        PluginDefinition(name: "SHIFTY", bundleIdentifier: "com.yoiboi.shifty", version: "0.5.0",
                         audioUnit: AudioUnitIdentity(type: "aufx", subtype: "Shf1", manufacturer: "Yoib")),
        PluginDefinition(name: "ENERGY HARVESTER", bundleIdentifier: "com.yoiboi.energyharvester", version: "0.4.0",
                         audioUnit: AudioUnitIdentity(type: "aufx", subtype: "Enh1", manufacturer: "Yoib"))
    ]
    /// Canonical order: each plug-in's VST3, then its AU.
    static let allItems: [ItemKey] = plugins.indices.flatMap { index in PluginFormat.allCases.map { ItemKey(plugin: index, format: $0) } }
    static func index(of key: ItemKey) -> Int { key.plugin * PluginFormat.allCases.count + (key.format == .vst3 ? 0 : 1) }
    static func plugin(named name: String) -> Int? { plugins.firstIndex { $0.name == name } }
}

/// Where everything lives. The shipping app uses `standard`; every self-test uses a temporary folder.
struct InstallLocations {
    var userVST3: URL
    var userComponents: URL
    var systemVST3: URL
    var systemComponents: URL
    /// Previous copies saved by the single-plugin installers when a rollback could not finish.
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

/// VST3 bundles carry component and controller class IDs; Audio Units carry their AudioComponents entries.
struct BundleIdentity: Codable, Equatable {
    let version: String
    var componentID: String?
    var controllerID: String?
    var audioUnits: [AudioUnitIdentity]?
}

struct PayloadEntry: Codable {
    let name: String
    let format: PluginFormat
    let filename: String
    let bundleIdentifier: String
    let identity: BundleIdentity
    let files: [String: String]
}

/// Format 2: ten entries in `SuiteCatalog.allItems` order.
struct SuiteManifest: Codable {
    let format: Int
    let version: String
    let items: [PayloadEntry]
    func entry(_ key: ItemKey) -> PayloadEntry { items[SuiteCatalog.index(of: key)] }
}

struct InstallResult {
    let installed: [ItemKey]
    let updated: [ItemKey]
    /// Selected items whose installed bytes already matched the payload; they were not touched.
    let skipped: [ItemKey]
    let audioUnitsRefreshed: Bool
    let log: [String]
    /// Problems that did not affect the installed plug-ins but that the user should know about.
    let warnings: [String]
}

/// What the window shows for one item before installing.
enum ItemState: Equatable {
    case notInstalled
    case older(String)
    case reinstall
    case upToDate
    case newer(String)
    case blocked(String)
}

extension ItemState {
    func label(newVersion: String) -> String {
        switch self {
        case .notInstalled: return "New"
        case .older(let installed): return "Installed \(installed) → \(newVersion)"
        case .reinstall: return "Reinstall \(newVersion)"
        case .upToDate: return "Up to date"
        case .newer(let installed): return "Newer \(installed) installed"
        case .blocked: return "Needs attention"
        }
    }
    var blocksInstall: Bool {
        switch self { case .newer, .blocked: return true; default: return false }
    }
}

/// State of one bundle before the transaction started.
struct SuiteSnapshot: Codable, Equatable {
    let filename: String
    let identity: BundleIdentity?
    let hashes: [String: String]?
    /// Absolute destination (journal format 3). Older journals imply the VST3 folder.
    var destination: String?
}

/// Per-item journal states: pending, committing (written before the rename), committed,
/// restoring (written before the rollback rename), restored, rollback-failed.
struct SuiteJournalItem: Codable, Equatable {
    let filename: String
    let stage: String
    var state: String
    var destination: String?
}

/// transaction.json. Format 3 records every item's destination (VST3 and AU); format 2 (suite 1.2.x fixes) and
/// format-1 journals (suite 1.2.2 and earlier) decode with those fields absent.
struct SuiteJournal: Codable {
    var format: Int?
    var state: String
    var destination: String
    var suiteVersion: String?
    var previous: [SuiteSnapshot]
    var payload: [String: [String: String]]?
    var items: [SuiteJournalItem]?
    var committed: [Int]?
}

// MARK: Hosts

enum InstallerHostDetection {
    static func isInstaller(_ identifier: String?) -> Bool {
        let id = (identifier ?? "").lowercased()
        return id == SuiteCatalog.installerIdentifier || id == "com.yoiboi.installer" ||
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
    /// Hosts not listed here are still caught by the open-file check while they have a VFD plug-in loaded.
    static let hostNames = ["ableton live", "bitwig studio", "reaper", "fl studio", "cubase", "nuendo", "studio one",
                            "digital performer", "renoise", "reason", "maschine 2", "komplete kontrol", "gig performer",
                            "vienna ensemble pro", "waveform", "pluginval"]
    /// Hosts that only load Audio Units: they must be quit only when an AU is being installed or removed.
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

/// Appends installer runs to one log file, rotating it to installer.log.1 when it grows past `limit` bytes.
enum InstallLog {
    static func append(_ lines: [String], to url: URL, limit: Int = 256 * 1024, date: Date = Date()) throws {
        let fm = FileManager.default
        try SuiteInstallerEngine.refuseSymlinkAncestors(url)
        try fm.createDirectory(at: url.deletingLastPathComponent(), withIntermediateDirectories: true)
        var data = Data()
        if SuiteInstallerEngine.exists(url) {
            data = try Data(contentsOf: url)
            if data.count > limit {
                let rotated = url.appendingPathExtension("1")
                try SuiteInstallerEngine.refuseSymlinkAncestors(rotated)
                if SuiteInstallerEngine.exists(rotated) { try fm.removeItem(at: rotated) }
                try fm.moveItem(at: url, to: rotated)
                data = Data()
            }
        }
        if !data.isEmpty { data.append(Data("\n".utf8)) }
        data.append(Data((ISO8601DateFormatter().string(from: date) + "\n" + lines.joined(separator: "\n") + "\n").utf8))
        try data.write(to: url, options: .atomic)
    }
}

// MARK: Engine

/// One transaction over the selected VST3 and AU bundles. The shipping UI supplies the user's plug-in folders;
/// every self-test supplies newly created temporary folders instead.
final class SuiteInstallerEngine {
    static let lockName = ".vfd-suite-installer.lock"
    static let transactionPrefix = ".vfd-suite-transaction-"
    static let discardPrefix = ".vfd-suite-discard-"
    /// Temporary copies left by the single-plugin installers, e.g. ".yoi-boi-stage-<UUID>.vst3" (1.x) or
    /// ".yoi-boi-stage-<UUID>" (current, no extension).
    static let singlePluginStagePattern = "^\\.[A-Za-z0-9 -]+-stage-[0-9A-F-]{36}(\\.vst3|\\.component)?$"
    let fm = FileManager.default
    let payloadRoot: URL
    let manifest: SuiteManifest
    let locations: InstallLocations
    /// Running hosts that must be quit, given the formats being changed.
    let runningHosts: (Set<PluginFormat>) -> [String]
    var log: [String] = []
    var progress: ((String) -> Void)?
    /// Makes macOS re-read its Audio Unit registrations. Replaced by a recorder in the self-tests.
    var refreshAudioUnits: () -> String = { SuiteInstallerEngine.refreshAudioUnitRegistrar() }
    /// Moves an item to the Trash and returns where it went. Replaced by a fixture Trash in the self-tests.
    var moveToTrash: (URL) throws -> URL? = { url in
        var result: NSURL?
        try FileManager.default.trashItem(at: url, resultingItemURL: &result)
        return result as URL?
    }
    // Fault hooks are available only to compiled self-tests, never command-line install options.
    var failAfterCommitForTesting: Int?
    var afterStagingForTesting: (() throws -> Void)?
    var afterCommitForTesting: ((Int) throws -> Void)?
    var journalObserverForTesting: ((SuiteJournal) -> Void)?

    init(payloadRoot: URL, manifest: SuiteManifest, locations: InstallLocations,
         runningHosts: @escaping (Set<PluginFormat>) -> [String]) {
        // The payload is only read, so it may live behind a symlink (App Translocation, a linked folder).
        self.payloadRoot = Self.canonical(payloadRoot)
        self.manifest = manifest
        self.locations = locations
        self.runningHosts = runningHosts
    }

    static func readManifest(_ url: URL) throws -> SuiteManifest {
        try JSONDecoder().decode(SuiteManifest.self, from: Data(contentsOf: url))
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
    static func sameItem(_ a: URL, _ b: URL) -> Bool {
        var left = stat(), right = stat()
        return lstat(a.path, &left) == 0 && lstat(b.path, &right) == 0 && left.st_dev == right.st_dev && left.st_ino == right.st_ino
    }
    /// Used for every location the installer writes to.
    static func refuseSymlinkAncestors(_ url: URL) throws {
        var current = URL(fileURLWithPath: "/", isDirectory: true)
        for component in url.pathComponents.dropFirst() {
            current.appendPathComponent(component)
            var info = stat()
            if lstat(current.path, &info) == 0 {
                guard (info.st_mode & S_IFMT) != S_IFLNK else {
                    throw InstallError.message("Use a normal folder, rather than a symbolic link: \(current.path)")
                }
            } else if errno != ENOENT {
                throw InstallError.message("Cannot inspect \(current.path): \(String(cString: strerror(errno)))")
            }
        }
    }
    static func fileHashes(in bundle: URL) throws -> [String: String] {
        guard isRealDirectory(bundle) else {
            throw InstallError.message("\(bundle.lastPathComponent) is missing, or is a link or file rather than a plug-in folder.")
        }
        guard let enumerator = FileManager.default.enumerator(atPath: bundle.path) else {
            throw InstallError.message("Cannot read \(bundle.lastPathComponent).")
        }
        var hashes: [String: String] = [:]
        for case let relative as String in enumerator {
            let path = bundle.path + "/" + relative
            var info = stat()
            guard lstat(path, &info) == 0 else { throw InstallError.message("Cannot inspect \(path).") }
            let type = info.st_mode & S_IFMT
            guard type == S_IFREG || type == S_IFDIR else {
                throw InstallError.message("Unexpected link or special file in \(bundle.lastPathComponent).")
            }
            if type == S_IFREG {
                let digest = SHA256.hash(data: try Data(contentsOf: URL(fileURLWithPath: path), options: .mappedIfSafe))
                hashes[relative] = digest.map { String(format: "%02x", $0) }.joined()
            }
        }
        guard !hashes.isEmpty else { throw InstallError.message("Empty plug-in: \(bundle.path)") }
        return hashes
    }
    /// Clears the download quarantine flag from a staged copy so Gatekeeper does not block the plug-in when a DAW
    /// loads it. Extended attributes are not part of the SHA-256 inventory or the code signature.
    static func stripQuarantine(in root: URL) throws {
        func strip(_ path: String) throws {
            if removexattr(path, "com.apple.quarantine", XATTR_NOFOLLOW) != 0 && errno != ENOATTR {
                throw InstallError.technical("The installer could not clear the download quarantine flag on its temporary copy.",
                                             detail: "removexattr \(path): \(String(cString: strerror(errno)))")
            }
        }
        try strip(root.path)
        guard let enumerator = FileManager.default.enumerator(atPath: root.path) else {
            throw InstallError.message("Cannot read \(root.lastPathComponent).")
        }
        for case let relative as String in enumerator { try strip(root.path + "/" + relative) }
    }
    @discardableResult static func run(_ executable: String, _ arguments: [String]) throws -> String {
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
            throw InstallError.message("\(URL(fileURLWithPath: executable).lastPathComponent) failed: \(text.trimmingCharacters(in: .whitespacesAndNewlines))")
        }
        return text
    }
    static func normalizedModuleInfo(_ text: String) -> String {
        // JUCE's VST3 metadata permits trailing commas; normalize those before parsing it.
        guard let expression = try? NSRegularExpression(pattern: ",\\s*([}\\]])") else { return text }
        return expression.stringByReplacingMatches(in: text, range: NSRange(text.startIndex..., in: text), withTemplate: "$1")
    }
    /// The AudioComponents entries of an Audio Unit bundle's Info.plist.
    static func audioUnitEntries(_ info: [String: Any]) -> [AudioUnitIdentity]? {
        guard let components = info["AudioComponents"] as? [[String: Any]], !components.isEmpty else { return nil }
        var result: [AudioUnitIdentity] = []
        for component in components {
            guard let type = component["type"] as? String, let subtype = component["subtype"] as? String,
                  let manufacturer = component["manufacturer"] as? String,
                  [type, subtype, manufacturer].allSatisfy({ $0.utf8.count == 4 }) else { return nil }
            result.append(AudioUnitIdentity(type: type, subtype: subtype, manufacturer: manufacturer))
        }
        return result
    }
    static func identity(at bundle: URL, definition: PluginDefinition, format: PluginFormat) throws -> BundleIdentity {
        guard isRealDirectory(bundle) else {
            throw InstallError.message("\(bundle.lastPathComponent) is not a plug-in folder. It has been left untouched.")
        }
        let object = try PropertyListSerialization.propertyList(
            from: Data(contentsOf: bundle.appendingPathComponent("Contents/Info.plist")), format: nil)
        guard let info = object as? [String: Any],
              info["CFBundleIdentifier"] as? String == definition.bundleIdentifier,
              info["CFBundleExecutable"] as? String == definition.name,
              let version = info["CFBundleShortVersionString"] as? String,
              version.range(of: "^[0-9]+(\\.[0-9]+){1,3}$", options: .regularExpression) != nil else {
            throw InstallError.message("\(bundle.lastPathComponent) is not the expected \(definition.name) bundle. It has been left untouched.")
        }
        if format == .au {
            guard let units = audioUnitEntries(info) else {
                throw InstallError.message("Cannot verify the Audio Unit identity of \(definition.name).")
            }
            return BundleIdentity(version: version, audioUnits: units)
        }
        let text = try String(contentsOf: bundle.appendingPathComponent("Contents/Resources/moduleinfo.json"), encoding: .utf8)
        guard let data = normalizedModuleInfo(text).data(using: .utf8),
              let module = try JSONSerialization.jsonObject(with: data) as? [String: Any],
              let classes = module["Classes"] as? [[String: Any]],
              let component = classes.first(where: { $0["Category"] as? String == "Audio Module Class" })?["CID"] as? String,
              let controller = classes.first(where: { $0["Category"] as? String == "Component Controller Class" })?["CID"] as? String,
              component.range(of: "^[0-9A-Fa-f]{32}$", options: .regularExpression) != nil,
              controller.range(of: "^[0-9A-Fa-f]{32}$", options: .regularExpression) != nil,
              component.uppercased() != controller.uppercased() else {
            throw InstallError.message("Cannot verify the VST3 identities of \(definition.name).")
        }
        return BundleIdentity(version: version, componentID: component.uppercased(), controllerID: controller.uppercased())
    }
    /// True when an installed bundle is the same plug-in as the payload (VST3 class IDs, or the AU's registered codes).
    static func sameProduct(_ installed: BundleIdentity, _ payload: BundleIdentity, format: PluginFormat, definition: PluginDefinition) -> Bool {
        if format == .vst3 { return installed.componentID == payload.componentID && installed.controllerID == payload.controllerID }
        return installed.audioUnits?.contains(definition.audioUnit) == true
    }
    /// Best-effort bundle identifier, VST3 class IDs and AU codes, used only to find duplicate copies.
    static func bundleSummary(at bundle: URL) -> (identifier: String?, classIDs: Set<String>, audioUnits: Set<AudioUnitIdentity>) {
        var identifier: String?
        var units = Set<AudioUnitIdentity>()
        if let data = try? Data(contentsOf: bundle.appendingPathComponent("Contents/Info.plist")),
           let info = (try? PropertyListSerialization.propertyList(from: data, format: nil)) as? [String: Any] {
            identifier = info["CFBundleIdentifier"] as? String
            units = Set(audioUnitEntries(info) ?? [])
        }
        var classIDs = Set<String>()
        if let text = try? String(contentsOf: bundle.appendingPathComponent("Contents/Resources/moduleinfo.json"), encoding: .utf8),
           let object = (try? JSONSerialization.jsonObject(with: Data(normalizedModuleInfo(text).utf8))) as? [String: Any],
           let classes = object["Classes"] as? [[String: Any]] {
            for entry in classes { if let id = entry["CID"] as? String { classIDs.insert(id.uppercased()) } }
        }
        return (identifier, classIDs, units)
    }

    // MARK: Manifest and payload

    func payloadURL(_ key: ItemKey) -> URL { payloadRoot.appendingPathComponent(key.filename) }
    func destinationURL(_ key: ItemKey) -> URL { locations.destinationRoot(key.format).appendingPathComponent(key.filename) }

    func validateManifest() throws {
        guard manifest.format == 2, manifest.items.count == SuiteCatalog.allItems.count,
              manifest.version.range(of: "^[0-9]+(\\.[0-9]+){1,3}$", options: .regularExpression) != nil else {
            throw InstallError.message("The suite manifest is invalid.")
        }
        var classIDs = Set<String>()
        var audioUnits = Set<AudioUnitIdentity>()
        for key in SuiteCatalog.allItems {
            let definition = key.definition
            let entry = manifest.entry(key)
            guard entry.name == definition.name, entry.format == key.format, entry.filename == key.filename,
                  entry.bundleIdentifier == definition.bundleIdentifier, entry.identity.version == definition.version,
                  !entry.files.isEmpty else { throw InstallError.message("The suite manifest does not match \(key.title).") }
            if key.format == .vst3 {
                for id in [entry.identity.componentID ?? "", entry.identity.controllerID ?? ""] {
                    guard id.range(of: "^[0-9A-F]{32}$", options: .regularExpression) != nil,
                          classIDs.insert(id).inserted else { throw InstallError.message("Invalid or duplicate VST3 identity in the suite.") }
                }
            } else {
                guard let units = entry.identity.audioUnits, units.contains(definition.audioUnit),
                      audioUnits.insert(definition.audioUnit).inserted else {
                    throw InstallError.message("The \(definition.name) Audio Unit identity in the manifest is not \(definition.audioUnit.description).")
                }
            }
        }
    }
    func verifyPayload(at bundle: URL, key: ItemKey) throws {
        let definition = key.definition
        let entry = manifest.entry(key)
        guard try Self.identity(at: bundle, definition: definition, format: key.format) == entry.identity else {
            throw InstallError.message("The \(key.title) payload identity does not match its manifest.")
        }
        guard fm.isExecutableFile(atPath: bundle.appendingPathComponent("Contents/MacOS/\(definition.name)").path) else {
            throw InstallError.message("\(key.title) has no executable permission.")
        }
        guard try Self.fileHashes(in: bundle) == entry.files else {
            throw InstallError.message("\(key.title) did not pass its SHA-256 integrity check. Obtain a fresh installer.")
        }
        do { try Self.run("/usr/bin/codesign", ["--verify", "--deep", "--strict", bundle.path]) }
        catch {
            throw InstallError.technical("\(key.title) did not pass its code-signature check. Download a fresh copy of the installer and try again.",
                                         detail: error.localizedDescription)
        }
    }
    func inspectPayloads(_ keys: [ItemKey] = SuiteCatalog.allItems) throws {
        try validateManifest()
        for key in keys { try verifyPayload(at: payloadURL(key), key: key) }
    }
    func note(_ message: String) { log.append(message); progress?(message) }
    func checkHosts(_ formats: Set<PluginFormat>) throws {
        let hosts = Array(Set(runningHosts(formats))).sorted()
        guard hosts.isEmpty else { throw InstallError.message("Quit \(hosts.joined(separator: ", ")) and try again.") }
    }

    // MARK: Status

    /// Read-only status of one item, shown beside its checkbox.
    func state(of key: ItemKey) -> ItemState {
        let entry = manifest.entry(key)
        let destination = destinationURL(key)
        do {
            try Self.refuseSymlinkAncestors(destination)
            guard Self.exists(destination) else { return .notInstalled }
            let identity = try Self.identity(at: destination, definition: key.definition, format: key.format)
            guard Self.sameProduct(identity, entry.identity, format: key.format, definition: key.definition) else {
                return .blocked("The installed \(key.title) uses different identities.")
            }
            if VersionOrder.compare(identity.version, entry.identity.version) == .orderedDescending { return .newer(identity.version) }
            if try Self.fileHashes(in: destination) == entry.files { return .upToDate }
            return VersionOrder.compare(identity.version, entry.identity.version) == .orderedSame ? .reinstall : .older(identity.version)
        } catch {
            return .blocked(UserFacingText.message(for: error))
        }
    }
    func states() -> [ItemKey: ItemState] {
        Dictionary(uniqueKeysWithValues: SuiteCatalog.allItems.map { ($0, state(of: $0)) })
    }

    // MARK: Duplicate copies

    /// Every bundle with `fileExtension` under `root`, without descending into bundles, symlinks or transaction folders.
    func pluginBundles(under root: URL, fileExtension: String) -> [URL] {
        guard let enumerator = fm.enumerator(at: root, includingPropertiesForKeys: [.isDirectoryKey, .isSymbolicLinkKey],
                                             options: [], errorHandler: { _, _ in true }) else { return [] }
        var bundles: [URL] = []
        for case let url as URL in enumerator {
            let name = url.lastPathComponent
            if name.hasPrefix(Self.transactionPrefix) || name.hasPrefix(Self.discardPrefix) { enumerator.skipDescendants(); continue }
            let values = try? url.resourceValues(forKeys: [.isDirectoryKey, .isSymbolicLinkKey])
            guard values?.isSymbolicLink != true, values?.isDirectory == true else { continue }
            let pathExtension = url.pathExtension.lowercased()
            if pathExtension == fileExtension { bundles.append(url) }
            if pathExtension == "vst3" || pathExtension == "component" { enumerator.skipDescendants() }
        }
        return bundles
    }
    /// Hosts load every copy in the plug-in folders and their subfolders, so any other bundle with a selected plug-in's
    /// bundle identifier, VST3 class ID or AU codes is a duplicate. Only provably redundant single-plugin leftovers are
    /// removed, and only when `cleanUp` is set (under the installation lock).
    func checkDuplicates(_ keys: [ItemKey], cleanUp: Bool) throws {
        for format in Set(keys.map(\.format)).sorted() {
            var roots: [(URL, Bool)] = []
            let userRoot = locations.destinationRoot(format)
            if Self.isRealDirectory(userRoot) { roots.append((userRoot, false)) }
            var systemInfo = stat()
            // The system folder is only read, so a relocated (symlinked) folder is followed rather than refused.
            if stat(locations.systemRoot(format).path, &systemInfo) == 0, (systemInfo.st_mode & S_IFMT) == S_IFDIR {
                roots.append((Self.canonical(locations.systemRoot(format)), true))
            }
            let selected = keys.filter { $0.format == format }
            for (root, isSystem) in roots {
                for candidate in pluginBundles(under: root, fileExtension: format.fileExtension) {
                    let summary = Self.bundleSummary(at: candidate)
                    for key in selected {
                        let entry = manifest.entry(key)
                        let matches: Bool
                        if format == .vst3 {
                            matches = summary.identifier == key.definition.bundleIdentifier ||
                                summary.classIDs.contains(entry.identity.componentID ?? "-") || summary.classIDs.contains(entry.identity.controllerID ?? "-")
                        } else {
                            matches = summary.identifier == key.definition.bundleIdentifier || summary.audioUnits.contains(key.definition.audioUnit)
                        }
                        guard matches else { continue }
                        let folder = format == .vst3 ? "/Library/Audio/Plug-Ins/VST3" : "/Library/Audio/Plug-Ins/Components"
                        if isSystem {
                            throw InstallError.message("A system-wide copy of \(key.title) is installed at:\n\(candidate.path)\n\nAudio apps would load both copies. Remove that copy from \(folder) (moving it to the Trash asks for an administrator password), then run this installer again. The suite installs its own copy in your user folder.")
                        }
                        let canonical = destinationURL(key)
                        if Self.sameItem(candidate, canonical) { continue }
                        let topLevel = Self.sameItem(candidate.deletingLastPathComponent(), userRoot)
                        if topLevel && candidate.lastPathComponent.range(of: Self.singlePluginStagePattern, options: .regularExpression) != nil {
                            if redundant(candidate, canonical: canonical, entry: entry) {
                                if cleanUp {
                                    try fm.removeItem(at: candidate)
                                    note("Removed a redundant temporary copy of \(key.title) left by a single-plugin installer: \(candidate.lastPathComponent)")
                                }
                                continue
                            }
                            throw InstallError.message("A temporary copy of \(key.title) was left behind by an interrupted single-plugin update:\n\(candidate.path)\n\nIt is hidden in your plug-in folder (press Command-Shift-Period in Finder to show hidden items). Move it to the Trash, or use Repair…, then try again. Your installed \(key.definition.name) is not affected.")
                        }
                        throw InstallError.message("Another copy of \(key.title) is in your plug-in folder:\n\(candidate.path)\n\nAudio apps load every copy in this folder and its subfolders, which causes duplicate plug-ins. Move that extra copy to the Trash or out of the folder, then try again.")
                    }
                }
            }
        }
    }
    private func redundant(_ copy: URL, canonical: URL, entry: PayloadEntry) -> Bool {
        guard let hashes = try? Self.fileHashes(in: copy) else { return false }
        if hashes == entry.files { return true }
        return Self.isRealDirectory(canonical) && (try? Self.fileHashes(in: canonical)) == hashes
    }

    // MARK: Snapshots

    private func snapshot(_ key: ItemKey) throws -> SuiteSnapshot {
        let entry = manifest.entry(key)
        let destination = destinationURL(key)
        try Self.refuseSymlinkAncestors(destination)
        guard Self.exists(destination) else {
            return SuiteSnapshot(filename: key.filename, identity: nil, hashes: nil, destination: destination.path)
        }
        let identity = try Self.identity(at: destination, definition: key.definition, format: key.format)
        guard Self.sameProduct(identity, entry.identity, format: key.format, definition: key.definition) else {
            let what = key.format == .vst3 ? "VST3 identities" : "Audio Unit codes"
            throw InstallError.message("The installed \(key.title) uses different \(what). It was left untouched to protect existing projects.")
        }
        guard VersionOrder.compare(identity.version, entry.identity.version) != .orderedDescending else {
            throw InstallError.message("\(key.title) \(identity.version) is newer than this suite’s \(entry.identity.version). Untick it to install the rest; no downgrade was performed.")
        }
        return SuiteSnapshot(filename: key.filename, identity: identity, hashes: try Self.fileHashes(in: destination), destination: destination.path)
    }
    private func verifyUnchanged(_ old: SuiteSnapshot, key: ItemKey) throws {
        let current = try snapshot(key)
        guard old.identity == current.identity, old.hashes == current.hashes else {
            throw InstallError.message("\(old.filename) changed while the suite was preparing. Please try again.")
        }
    }

    // MARK: Lock, permissions and leftovers

    static func acquireLock(in folder: URL) throws -> Int32 {
        let lockURL = folder.appendingPathComponent(lockName)
        let lock = open(lockURL.path, O_CREAT | O_RDWR | O_NOFOLLOW | O_CLOEXEC, S_IRUSR | S_IWUSR)
        guard lock >= 0 else {
            let code = errno
            let reason = String(cString: strerror(code))
            if code == ELOOP {
                throw InstallError.message("The installation lock file is a symbolic link, so the installer stopped for safety:\n\(lockURL.path)\n\nMove that item to the Trash, then try again.")
            }
            if code == EACCES || code == EPERM {
                throw InstallError.message("The installer can’t open its lock file (\(reason)):\n\(lockURL.path)\n\nThe file or your plug-in folder may belong to another user or an administrator. In Finder, select the folder, choose File > Get Info, and give your account Read & Write permission, then try again.")
            }
            throw InstallError.message("The installer can’t open its lock file (\(reason)):\n\(lockURL.path)")
        }
        var info = stat()
        guard fstat(lock, &info) == 0, (info.st_mode & S_IFMT) == S_IFREG, info.st_nlink == 1, info.st_uid == geteuid() else {
            close(lock)
            throw InstallError.message("The installation lock file is not a normal file owned by you, so the installer stopped for safety:\n\(lockURL.path)\n\nMove that file to the Trash, then try again.")
        }
        guard flock(lock, LOCK_EX | LOCK_NB) == 0 else {
            let code = errno
            close(lock)
            if code == EWOULDBLOCK {
                throw InstallError.message("Another Vivid Fever Dreams installer is changing your plug-in folders right now. Let it finish (or quit it), then try again.")
            }
            throw InstallError.message("The installer couldn’t lock \(lockURL.path) (\(String(cString: strerror(code)))).")
        }
        return lock
    }
    /// Locks both plug-in folders in a fixed order: the formats in `create` are created when missing, the others are
    /// locked only if they already exist. Returns the locks and the folders they cover.
    func lockFolders(create: Set<PluginFormat>) throws -> (locks: [Int32], roots: [URL]) {
        var locks: [Int32] = []
        var roots: [URL] = []
        do {
            for format in PluginFormat.allCases.sorted() {
                let root = locations.destinationRoot(format)
                try Self.refuseSymlinkAncestors(root)
                if create.contains(format) {
                    do { try fm.createDirectory(at: root, withIntermediateDirectories: true) }
                    catch { throw InstallError.technical("The installer couldn’t create your \(format.label) folder:\n\(root.path)", detail: "\(error)") }
                    try Self.refuseSymlinkAncestors(root)
                    try requireWritable(root, format: format)
                } else if !Self.isRealDirectory(root) { continue }
                locks.append(try Self.acquireLock(in: root))
                roots.append(root)
            }
        } catch {
            Self.unlock(locks)
            throw error
        }
        return (locks, roots)
    }
    static func unlock(_ locks: [Int32]) { for lock in locks { flock(lock, LOCK_UN); close(lock) } }
    private func requireWritable(_ root: URL, format: PluginFormat) throws {
        guard access(root.path, W_OK | X_OK) == 0 else {
            let reason = String(cString: strerror(errno))
            throw InstallError.message("Your \(format.label) folder can’t be changed by your user account (\(reason)):\n\(root.path)\n\nThis usually happens when another installer created the folder as an administrator. In Finder, select the folder, choose File > Get Info, and give your account Read & Write permission, then try again.")
        }
    }
    /// Each bundle a journal covers: where it lives, what it was before, what the payload was, and its stage name.
    struct JournalTarget {
        let filename: String
        let destination: URL
        let previous: [String: String]?
        let previousRecorded: Bool
        let payload: [String: String]?
        let stage: String
        let state: String?
    }
    static func readJournal(_ folder: URL) -> SuiteJournal? {
        guard let data = try? Data(contentsOf: folder.appendingPathComponent("transaction.json")) else { return nil }
        return try? JSONDecoder().decode(SuiteJournal.self, from: data)
    }
    /// nil when the journal does not describe bundles in this user's plug-in folders.
    func journalTargets(_ journal: SuiteJournal, root: URL) -> [JournalTarget]? {
        let roots = [locations.userVST3.path, locations.userComponents.path]
        if (journal.format ?? 1) >= 3 {
            var targets: [JournalTarget] = []
            for snapshot in journal.previous {
                guard let path = snapshot.destination else { return nil }
                let destination = URL(fileURLWithPath: path)
                guard roots.contains(destination.deletingLastPathComponent().path), destination.lastPathComponent == snapshot.filename else { return nil }
                let item = journal.items?.first { $0.filename == snapshot.filename }
                targets.append(JournalTarget(filename: snapshot.filename, destination: destination, previous: snapshot.hashes, previousRecorded: true,
                                             payload: journal.payload?[snapshot.filename], stage: item?.stage ?? "", state: item?.state))
            }
            return targets.isEmpty ? nil : targets
        }
        // Formats 1 and 2 covered the five VST3 bundles in the VST3 folder.
        guard journal.destination == root.path, root.path == locations.userVST3.path,
              journal.previous.map(\.filename) == SuiteCatalog.plugins.map({ $0.filename(for: .vst3) }) else { return nil }
        return journal.previous.enumerated().map { index, snapshot in
            let item = journal.items?.first { $0.filename == snapshot.filename }
            let committed = journal.committed?.contains(index) == true
            return JournalTarget(filename: snapshot.filename, destination: root.appendingPathComponent(snapshot.filename),
                                 previous: snapshot.hashes, previousRecorded: true, payload: journal.payload?[snapshot.filename],
                                 stage: item?.stage ?? snapshot.filename, state: item?.state ?? (committed ? "committed" : nil))
        }
    }
    func matches(_ url: URL, _ hashes: [String: String]?) -> Bool {
        if let hashes = hashes { return Self.isRealDirectory(url) && (try? Self.fileHashes(in: url)) == hashes }
        return !Self.exists(url)
    }
    /// A reason string when an interrupted transaction folder provably holds nothing that is still needed.
    func provablySafeLeftover(_ folder: URL, root: URL) -> String? {
        guard Self.isRealDirectory(folder), let contents = try? fm.contentsOfDirectory(atPath: folder.path) else { return nil }
        if contents.isEmpty { return "it was empty" }
        guard let journal = Self.readJournal(folder), let targets = journalTargets(journal, root: root) else { return nil }
        switch journal.state {
        case "staging", "prepared", "rolled-back":
            // Every destination still matches the recorded previous state, so no plug-in was left replaced.
            return targets.allSatisfy { matches($0.destination, $0.previous) } ? "no installed plug-in had been replaced" : nil
        case "all-verified":
            guard targets.allSatisfy({ $0.payload != nil }) else { return nil }
            return targets.allSatisfy { matches($0.destination, $0.payload) } ? "it had already finished" : nil
        default:
            return nil
        }
    }
    /// Runs under the lock. Finished leftovers are removed; an unfinished transaction is removed only when that is
    /// provably safe, otherwise installation stops with directions.
    func resolveLeftovers(in root: URL) throws -> [String] {
        var warnings: [String] = []
        let names = try fm.contentsOfDirectory(atPath: root.path).sorted()
        for name in names where name.hasPrefix(Self.discardPrefix) {
            if let warning = removeDiscarded(root.appendingPathComponent(name)) { warnings.append(warning) }
        }
        for name in names where name.hasPrefix(Self.transactionPrefix) {
            let folder = root.appendingPathComponent(name)
            if let reason = provablySafeLeftover(folder, root: root) {
                note("Cleaned up an interrupted suite installation (\(reason)): \(name)")
                if let warning = discardTransaction(folder) { warnings.append(warning) }
                continue
            }
            let recorded = Self.readJournal(folder).map { "\n\nRecorded state: \($0.state)." } ?? ""
            throw InstallError.message("A previous suite installation was interrupted, and the installer can’t prove that cleaning it up is safe, so nothing was changed.\n\nIts backup copies and a record of what happened are kept in this hidden folder:\n\(folder.path)\n\nClick Repair… to restore your previous plug-ins or clean it up. In Finder, press Command-Shift-Period to see hidden folders.\(recorded)")
        }
        return warnings
    }
    /// Renames a finished transaction to a discard name first, so a partial delete can never leave a folder that
    /// looks like an unfinished transaction, then deletes it. Returns a warning when the delete fails.
    func discardTransaction(_ transaction: URL) -> String? {
        guard Self.exists(transaction) else { return nil }
        let discard = transaction.deletingLastPathComponent().appendingPathComponent(Self.discardPrefix + UUID().uuidString)
        let target = renameatx_np(AT_FDCWD, transaction.path, AT_FDCWD, discard.path, UInt32(RENAME_EXCL)) == 0 ? discard : transaction
        return removeDiscarded(target)
    }
    private func removeDiscarded(_ folder: URL) -> String? {
        do { try fm.removeItem(at: folder); return nil }
        catch {
            let warning = "The installer could not delete its temporary folder, which only holds copies that are no longer needed:\n\(folder.path)\n(\(error.localizedDescription)) The next installation will try again, or you can move it to the Trash."
            log.append(warning)
            return warning
        }
    }

    // MARK: Install

    func install(selection: Set<ItemKey>) throws -> InstallResult {
        let keys = selection.sorted()
        guard !keys.isEmpty else { throw InstallError.message("Select at least one plug-in format to install.") }
        let formats = Set(keys.map(\.format))
        log = ["Vivid Fever Dreams Suite \(manifest.version)", "Selected: \(keys.map(\.title).joined(separator: ", "))"]
        for format in formats.sorted() { log.append("\(format.label) folder: \(locations.destinationRoot(format).path)") }
        var warnings: [String] = []
        try checkHosts(formats)
        for format in formats.sorted() { try Self.refuseSymlinkAncestors(locations.destinationRoot(format)) }
        try inspectPayloads(keys)
        note("The selected payloads passed identity, SHA-256 and signature checks.")
        try checkDuplicates(keys, cleanUp: false)
        let locked = try lockFolders(create: formats)
        defer { Self.unlock(locked.locks) }
        let roots = locked.roots
        let selectedRoots = formats.sorted().map { locations.destinationRoot($0) }
        var rootInfo = stat(), otherInfo = stat()
        if selectedRoots.count == 2, stat(selectedRoots[0].path, &rootInfo) == 0, stat(selectedRoots[1].path, &otherInfo) == 0,
           rootInfo.st_dev != otherInfo.st_dev {
            throw InstallError.message("Your VST3 and Components folders are on different disks, so they can’t be changed in one safe step. Install the VST3 and AU formats separately.")
        }
        for root in roots { warnings += try resolveLeftovers(in: root) }
        try checkDuplicates(keys, cleanUp: true)
        var previous: [ItemKey: SuiteSnapshot] = [:]
        var work: [ItemKey] = []
        var skipped: [ItemKey] = []
        for key in keys {
            let snapshot = try snapshot(key)
            previous[key] = snapshot
            if snapshot.hashes == manifest.entry(key).files { skipped.append(key) } else { work.append(key) }
        }
        if !skipped.isEmpty { note("Already up to date, left untouched: \(skipped.map(\.title).joined(separator: ", ")).") }
        guard let first = work.first else {
            note("Everything selected is already up to date.")
            return InstallResult(installed: [], updated: [], skipped: skipped, audioUnitsRefreshed: false, log: log, warnings: warnings)
        }
        let transaction = locations.destinationRoot(first.format).appendingPathComponent(Self.transactionPrefix + UUID().uuidString, isDirectory: true)
        try fm.createDirectory(at: transaction, withIntermediateDirectories: false, attributes: [.posixPermissions: 0o700])
        // Extension-less stage names: audio apps and the AU registrar never see staged or retained copies.
        func stageURL(_ position: Int) -> URL { transaction.appendingPathComponent("\(position).staged", isDirectory: true) }
        var state = "staging"
        var items = work.enumerated().map {
            SuiteJournalItem(filename: $1.filename, stage: "\($0).staged", state: "pending", destination: destinationURL($1).path)
        }
        var committed: [Int] = []
        let previousList = work.compactMap { previous[$0] }
        let payloadHashes = Dictionary(uniqueKeysWithValues: work.map { ($0.filename, manifest.entry($0).files) })
        func journal() throws {
            let record = SuiteJournal(format: 3, state: state, destination: transaction.deletingLastPathComponent().path, suiteVersion: manifest.version,
                                      previous: previousList, payload: payloadHashes, items: items, committed: committed)
            let encoder = JSONEncoder(); encoder.outputFormatting = [.prettyPrinted, .sortedKeys]
            try encoder.encode(record).write(to: transaction.appendingPathComponent("transaction.json"), options: .atomic)
            journalObserverForTesting?(record)
        }
        do {
            try journal()
            for (position, key) in work.enumerated() {
                let stage = stageURL(position)
                try fm.copyItem(at: payloadURL(key), to: stage)
                try Self.stripQuarantine(in: stage)
                try verifyPayload(at: stage, key: key)
            }
            note("All \(work.count) plug-in bundles staged and verified; installed copies are still untouched.")
            try afterStagingForTesting?()
            try checkHosts(formats)
            try checkDuplicates(work, cleanUp: false)
            for key in work { if let old = previous[key] { try verifyUnchanged(old, key: key) } }
            state = "prepared"
            try journal()
            state = "committing"
            for (position, key) in work.enumerated() {
                try checkHosts(formats)
                try checkDuplicates([key], cleanUp: false)
                if let old = previous[key] { try verifyUnchanged(old, key: key) }
                let stage = stageURL(position)
                try verifyPayload(at: stage, key: key)
                let destination = destinationURL(key)
                // Record the intent before the rename: after a crash, "committing" means "check by hash".
                items[position].state = "committing"
                try journal()
                let flags = previous[key]?.identity == nil ? UInt32(RENAME_EXCL) : UInt32(RENAME_SWAP)
                guard renameatx_np(AT_FDCWD, stage.path, AT_FDCWD, destination.path, flags) == 0 else {
                    let code = errno
                    items[position].state = "pending"
                    try? journal()
                    throw InstallError.technical("macOS did not allow the installer to place \(key.title) in your plug-in folder (\(String(cString: strerror(code)))).",
                                                 detail: "renameatx_np \(stage.path) -> \(destination.path) flags \(flags): errno \(code)")
                }
                committed.append(position)
                items[position].state = "committed"
                try journal()
                try afterCommitForTesting?(committed.count)
                if failAfterCommitForTesting == committed.count { throw InstallError.message("Injected failure after commit \(committed.count)") }
                try Self.refuseSymlinkAncestors(destination)
                try verifyPayload(at: destination, key: key)
                note("Verified \(key.title) \(manifest.entry(key).identity.version); retaining previous bundles until the transaction is complete.")
            }
            for key in work {
                let destination = destinationURL(key)
                try Self.refuseSymlinkAncestors(destination)
                try verifyPayload(at: destination, key: key)
            }
            state = "all-verified"
            try journal()
            note("All \(work.count) installed bundles verified. Previous transaction copies can now be removed.")
        } catch {
            log.append("Transaction failed: \(UserFacingText.logText(for: error))")
            // An intent recorded just before a rename that never happened is still pending.
            for index in items.indices where items[index].state == "committing" && !committed.contains(index) {
                items[index].state = "pending"
            }
            state = "rolling-back"
            try? journal()
            var rollbackErrors: [String] = []
            for position in committed.reversed() {
                let key = work[position]
                let entry = manifest.entry(key)
                let destination = destinationURL(key)
                let stage = stageURL(position)
                items[position].state = "restoring"
                try? journal()
                do {
                    // Never erase an independently changed destination during recovery.
                    try Self.refuseSymlinkAncestors(destination)
                    guard try Self.fileHashes(in: destination) == entry.files else {
                        throw InstallError.message("The destination changed outside this transaction.")
                    }
                    let old = previous[key]
                    let wasAbsent = old?.identity == nil
                    let source = wasAbsent ? destination : stage
                    let target = wasAbsent ? stage : destination
                    let flags = wasAbsent ? UInt32(RENAME_EXCL) : UInt32(RENAME_SWAP)
                    guard renameatx_np(AT_FDCWD, source.path, AT_FDCWD, target.path, flags) == 0 else {
                        throw InstallError.message(String(cString: strerror(errno)))
                    }
                    if let hashes = old?.hashes {
                        guard try Self.fileHashes(in: destination) == hashes else { throw InstallError.message("Restored copy failed its integrity check.") }
                    } else if Self.exists(destination) { throw InstallError.message("New installation could not be removed.") }
                    // The stage now holds the new copy that was rolled back. Remove it so retained data is never ambiguous.
                    if Self.exists(stage) { try? fm.removeItem(at: stage) }
                    items[position].state = "restored"
                    try? journal()
                    log.append("Restored previous state of \(key.title).")
                } catch {
                    items[position].state = "rollback-failed"
                    try? journal()
                    rollbackErrors.append("\(key.title): \(error.localizedDescription)")
                }
            }
            if !rollbackErrors.isEmpty {
                state = "recovery-required"
                try? journal()
                throw InstallError.message("Installation failed, and the installer could not fully restore your previous plug-ins. Nothing will be deleted. Backup copies and a record of each plug-in’s state are kept in this hidden folder:\n\(transaction.path)\n\n\(rollbackErrors.joined(separator: "\n"))\n\nClick Repair… to restore the previous versions. Original problem: \(UserFacingText.message(for: error))")
            }
            state = "rolled-back"
            try? journal()
            if !committed.isEmpty { note("The transaction was rolled back; prior installed bundles were restored.") }
            _ = discardTransaction(transaction)
            throw error
        }
        if let warning = discardTransaction(transaction) { warnings.append(warning) }
        var refreshed = false
        if work.contains(where: { $0.format == .au }) {
            note(refreshAudioUnits())
            refreshed = true
        }
        note("Saved presets and DAW projects were not modified.")
        let updated = work.filter { previous[$0]?.identity != nil }
        let installed = work.filter { previous[$0]?.identity == nil }
        return InstallResult(installed: installed, updated: updated, skipped: skipped, audioUnitsRefreshed: refreshed, log: log, warnings: warnings)
    }
}
