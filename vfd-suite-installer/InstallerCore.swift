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
            return "macOS did not allow the installer to change a file it needs\(pathSuffix(nsError)). Make sure your user account can write to ~/Library/Audio/Plug-Ins/VST3, then try again."
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
    case renderPreview(String)
    case invalid
    static func parse(_ rawArguments: [String]) -> LaunchMode {
        let arguments = Array(rawArguments.dropFirst().filter { !$0.hasPrefix("-psn_") })
        guard let flag = arguments.first(where: { $0.hasPrefix("--") }) else { return .gui }
        guard arguments.first == flag else { return .invalid }
        switch (flag, arguments.count) {
        case ("--self-test", 1): return .selfTest
        case ("--inspect-payload", 1): return .inspectPayload
        case ("--check-hosts", 1): return .checkHosts
        case ("--render-preview", 2): return .renderPreview(arguments[1])
        case ("--make-manifest", 4): return .makeManifest(payload: arguments[1], manifest: arguments[2], version: arguments[3])
        default: return .invalid
        }
    }
}

struct PluginDefinition {
    let name: String
    let bundleIdentifier: String
    let version: String
    var filename: String { name + ".vst3" }
}

enum SuiteCatalog {
    static let installerIdentifier = "xyz.vividfeverdreams.suite.installer"
    static let plugins = [
        PluginDefinition(name: "YOI-BOI", bundleIdentifier: "com.yoiboi.synth", version: "0.1.6"),
        PluginDefinition(name: "THROW-UP", bundleIdentifier: "com.yoiboi.throwup", version: "0.1.5"),
        PluginDefinition(name: "BACK-SCRATCH", bundleIdentifier: "com.yoiboi.backscratch", version: "0.10.2"),
        PluginDefinition(name: "SHIFTY", bundleIdentifier: "com.yoiboi.shifty", version: "0.4.2"),
        PluginDefinition(name: "ENERGY HARVESTER", bundleIdentifier: "com.yoiboi.energyharvester", version: "0.3.2")
    ]
}

struct PluginIdentity: Equatable, Codable {
    let version: String
    let componentID: String
    let controllerID: String
}

struct PayloadManifest: Codable {
    let name: String
    let filename: String
    let bundleIdentifier: String
    let identity: PluginIdentity
    let files: [String: String]
}

struct SuiteManifest: Codable {
    let format: Int
    let version: String
    let plugins: [PayloadManifest]
}

struct InstallResult {
    let installed: Int
    let updated: Int
    let destination: URL
    let log: [String]
    /// Problems that did not affect the installed plug-ins but that the user should know about.
    let warnings: [String]
}

/// State of one plug-in bundle as it was before the transaction started.
struct SuiteSnapshot: Codable, Equatable {
    let filename: String
    let identity: PluginIdentity?
    let hashes: [String: String]?
}

/// Per-item journal states: pending, committing (written before the rename), committed,
/// restoring (written before the rollback rename), restored, rollback-failed.
struct SuiteJournalItem: Codable, Equatable {
    let filename: String
    let stage: String
    var state: String
}

/// transaction.json. Format 2 adds payload hashes and per-item states; format-1 journals (suite 1.2.2 and
/// earlier) decode with those fields absent.
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

enum InstallerHostDetection {
    static func isInstaller(_ identifier: String?) -> Bool {
        let id = (identifier ?? "").lowercased()
        return id == SuiteCatalog.installerIdentifier || id == "com.yoiboi.installer" ||
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
    /// Hosts not listed here are still caught by the open-file check while they have a VFD plug-in loaded.
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

/// One five-payload transaction. The shipping UI supplies the user's VST3 folder;
/// every self-test supplies a newly created temporary directory instead.
final class SuiteInstallerEngine {
    static let lockName = ".vfd-suite-installer.lock"
    static let transactionPrefix = ".vfd-suite-transaction-"
    static let discardPrefix = ".vfd-suite-discard-"
    /// Temporary copies left by the single-plugin installers (1.x stage names), e.g. ".yoi-boi-stage-<UUID>.vst3".
    static let singlePluginStagePattern = "^\\.[A-Za-z0-9 -]+-stage-[0-9A-F-]{36}\\.vst3$"
    private let fm = FileManager.default
    let payloadRoot: URL
    let manifest: SuiteManifest
    let destinationRoot: URL
    let systemRoot: URL
    let runningHosts: () -> [String]
    var log: [String] = []
    var progress: ((String) -> Void)?
    // Fault hooks are available only to compiled self-tests, never command-line install options.
    var failAfterCommitForTesting: Int?
    var afterStagingForTesting: (() throws -> Void)?
    var afterCommitForTesting: ((Int) throws -> Void)?
    var journalObserverForTesting: ((SuiteJournal) -> Void)?

    init(payloadRoot: URL, manifest: SuiteManifest, destinationRoot: URL, systemRoot: URL,
         runningHosts: @escaping () -> [String]) {
        // The payload is only read, so it may live behind a symlink (App Translocation, a linked folder).
        self.payloadRoot = Self.canonical(payloadRoot)
        self.manifest = manifest
        self.destinationRoot = destinationRoot
        self.systemRoot = systemRoot
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
    private static func normalizedModuleInfo(_ text: String) -> String {
        // JUCE's VST3 metadata permits trailing commas; normalize those before parsing it.
        guard let expression = try? NSRegularExpression(pattern: ",\\s*([}\\]])") else { return text }
        return expression.stringByReplacingMatches(in: text, range: NSRange(text.startIndex..., in: text), withTemplate: "$1")
    }
    static func identity(at bundle: URL, definition: PluginDefinition) throws -> PluginIdentity {
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
        return PluginIdentity(version: version, componentID: component.uppercased(), controllerID: controller.uppercased())
    }
    /// Best-effort bundle identifier and VST3 class IDs, used only to find duplicate copies.
    static func bundleSummary(at bundle: URL) -> (identifier: String?, classIDs: Set<String>) {
        var identifier: String?
        if let data = try? Data(contentsOf: bundle.appendingPathComponent("Contents/Info.plist")),
           let info = (try? PropertyListSerialization.propertyList(from: data, format: nil)) as? [String: Any] {
            identifier = info["CFBundleIdentifier"] as? String
        }
        var classIDs = Set<String>()
        if let text = try? String(contentsOf: bundle.appendingPathComponent("Contents/Resources/moduleinfo.json"), encoding: .utf8),
           let object = (try? JSONSerialization.jsonObject(with: Data(normalizedModuleInfo(text).utf8))) as? [String: Any],
           let classes = object["Classes"] as? [[String: Any]] {
            for entry in classes { if let id = entry["CID"] as? String { classIDs.insert(id.uppercased()) } }
        }
        return (identifier, classIDs)
    }
    func validateManifest() throws {
        guard manifest.format == 1, manifest.plugins.count == SuiteCatalog.plugins.count,
              manifest.version.range(of: "^[0-9]+(\\.[0-9]+){1,3}$", options: .regularExpression) != nil else {
            throw InstallError.message("The suite manifest is invalid.")
        }
        var allIDs = Set<String>()
        for (definition, entry) in zip(SuiteCatalog.plugins, manifest.plugins) {
            guard entry.name == definition.name, entry.filename == definition.filename,
                  entry.bundleIdentifier == definition.bundleIdentifier, entry.identity.version == definition.version,
                  !entry.files.isEmpty else { throw InstallError.message("The suite manifest does not match \(definition.name).") }
            for id in [entry.identity.componentID, entry.identity.controllerID] {
                guard id.range(of: "^[0-9A-F]{32}$", options: .regularExpression) != nil,
                      allIDs.insert(id).inserted else { throw InstallError.message("Invalid or duplicate VST3 identity in the suite.") }
            }
        }
    }
    func verifyPayload(at bundle: URL, index: Int) throws {
        let definition = SuiteCatalog.plugins[index]
        let entry = manifest.plugins[index]
        guard try Self.identity(at: bundle, definition: definition) == entry.identity else {
            throw InstallError.message("The \(definition.name) payload identity does not match its manifest.")
        }
        guard fm.isExecutableFile(atPath: bundle.appendingPathComponent("Contents/MacOS/\(definition.name)").path) else {
            throw InstallError.message("\(definition.name) has no executable permission.")
        }
        guard try Self.fileHashes(in: bundle) == entry.files else {
            throw InstallError.message("\(definition.name) did not pass its SHA-256 integrity check. Obtain a fresh installer.")
        }
        do { try Self.run("/usr/bin/codesign", ["--verify", "--deep", "--strict", bundle.path]) }
        catch {
            throw InstallError.technical("\(definition.name) did not pass its code-signature check. Download a fresh copy of the installer and try again.",
                                         detail: error.localizedDescription)
        }
    }
    func inspectPayloads() throws {
        try validateManifest()
        for (index, entry) in manifest.plugins.enumerated() {
            try verifyPayload(at: payloadRoot.appendingPathComponent(entry.filename), index: index)
        }
    }
    private func note(_ message: String) { log.append(message); progress?(message) }
    private func checkHosts() throws {
        let hosts = Array(Set(runningHosts())).sorted()
        guard hosts.isEmpty else { throw InstallError.message("Quit \(hosts.joined(separator: ", ")) and try Install / Update again.") }
    }

    // MARK: Duplicate copies

    /// Every .vst3 bundle under `root`, without descending into bundles, symlinks or transaction folders.
    private func pluginBundles(under root: URL) -> [URL] {
        guard let enumerator = fm.enumerator(at: root, includingPropertiesForKeys: [.isDirectoryKey, .isSymbolicLinkKey],
                                             options: [], errorHandler: { _, _ in true }) else { return [] }
        var bundles: [URL] = []
        for case let url as URL in enumerator {
            let name = url.lastPathComponent
            if name.hasPrefix(Self.transactionPrefix) || name.hasPrefix(Self.discardPrefix) { enumerator.skipDescendants(); continue }
            let values = try? url.resourceValues(forKeys: [.isDirectoryKey, .isSymbolicLinkKey])
            guard values?.isSymbolicLink != true, values?.isDirectory == true else { continue }
            if url.pathExtension.lowercased() == "vst3" { bundles.append(url); enumerator.skipDescendants() }
        }
        return bundles
    }
    /// Hosts load every copy in the VST3 folders and their subfolders, so any other bundle with a suite plug-in's
    /// bundle identifier or VST3 class ID is a duplicate. Only provably redundant single-plugin leftovers are removed,
    /// and only when `cleanUp` is set (under the installation lock).
    private func checkDuplicates(cleanUp: Bool) throws {
        var roots: [(URL, Bool)] = []
        if Self.isRealDirectory(destinationRoot) { roots.append((destinationRoot, false)) }
        var systemInfo = stat()
        // The system folder is only read, so a relocated (symlinked) folder is followed rather than refused.
        if stat(systemRoot.path, &systemInfo) == 0, (systemInfo.st_mode & S_IFMT) == S_IFDIR {
            roots.append((Self.canonical(systemRoot), true))
        }
        for (root, isSystem) in roots {
            for candidate in pluginBundles(under: root) {
                let summary = Self.bundleSummary(at: candidate)
                for (index, definition) in SuiteCatalog.plugins.enumerated() {
                    let entry = manifest.plugins[index]
                    let sameIdentifier = summary.identifier == definition.bundleIdentifier
                    let sameClass = summary.classIDs.contains(entry.identity.componentID) ||
                        summary.classIDs.contains(entry.identity.controllerID)
                    guard sameIdentifier || sameClass else { continue }
                    if isSystem {
                        throw InstallError.message("A system-wide copy of \(definition.name) is installed at:\n\(candidate.path)\n\nAudio apps would load both copies. Remove that copy (moving it to the Trash asks for an administrator password), then run this installer again. The suite installs its own copy in your user VST3 folder.")
                    }
                    let canonical = destinationRoot.appendingPathComponent(definition.filename)
                    if Self.sameItem(candidate, canonical) { continue }
                    let topLevel = Self.sameItem(candidate.deletingLastPathComponent(), destinationRoot)
                    if topLevel && candidate.lastPathComponent.range(of: Self.singlePluginStagePattern, options: .regularExpression) != nil {
                        if redundant(candidate, canonical: canonical, entry: entry) {
                            if cleanUp {
                                try fm.removeItem(at: candidate)
                                note("Removed a redundant temporary copy of \(definition.name) left by a single-plugin installer: \(candidate.lastPathComponent)")
                            }
                            continue
                        }
                        throw InstallError.message("A temporary copy of \(definition.name) was left behind by an interrupted single-plugin update:\n\(candidate.path)\n\nIt is hidden in your VST3 folder (press Command-Shift-Period in Finder to show hidden items). Move it to the Trash, then try again. Your installed \(definition.name) is not affected.")
                    }
                    throw InstallError.message("Another copy of \(definition.name) is in your VST3 folder:\n\(candidate.path)\n\nAudio apps load every copy in this folder and its subfolders, which causes duplicate plug-ins. Move that extra copy to the Trash or out of the VST3 folder, then try again.")
                }
            }
        }
    }
    private func redundant(_ copy: URL, canonical: URL, entry: PayloadManifest) -> Bool {
        guard let hashes = try? Self.fileHashes(in: copy) else { return false }
        if hashes == entry.files { return true }
        return Self.isRealDirectory(canonical) && (try? Self.fileHashes(in: canonical)) == hashes
    }

    // MARK: Snapshots

    private func snapshot(_ index: Int) throws -> SuiteSnapshot {
        let definition = SuiteCatalog.plugins[index]
        let entry = manifest.plugins[index]
        let destination = destinationRoot.appendingPathComponent(definition.filename)
        try Self.refuseSymlinkAncestors(destination)
        guard Self.exists(destination) else { return SuiteSnapshot(filename: definition.filename, identity: nil, hashes: nil) }
        let identity = try Self.identity(at: destination, definition: definition)
        guard identity.componentID == entry.identity.componentID, identity.controllerID == entry.identity.controllerID else {
            throw InstallError.message("The installed \(definition.name) uses different VST3 identities. It was left untouched to protect existing projects.")
        }
        guard VersionOrder.compare(identity.version, entry.identity.version) != .orderedDescending else {
            throw InstallError.message("\(definition.name) \(identity.version) is newer than this suite’s \(entry.identity.version). No downgrade was performed.")
        }
        return SuiteSnapshot(filename: definition.filename, identity: identity, hashes: try Self.fileHashes(in: destination))
    }
    private func verifyUnchanged(_ old: SuiteSnapshot, index: Int) throws {
        let current = try snapshot(index)
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
                throw InstallError.message("The installer can’t open its lock file (\(reason)):\n\(lockURL.path)\n\nThe file or your VST3 folder may belong to another user or an administrator. In Finder, select the VST3 folder, choose File > Get Info, and give your account Read & Write permission, then try again.")
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
                throw InstallError.message("Another Vivid Fever Dreams installer is changing your VST3 folder right now. Let it finish (or quit it), then try again.")
            }
            throw InstallError.message("The installer couldn’t lock \(lockURL.path) (\(String(cString: strerror(code)))).")
        }
        return lock
    }
    private func requireWritableDestination() throws {
        guard access(destinationRoot.path, W_OK | X_OK) == 0 else {
            let reason = String(cString: strerror(errno))
            throw InstallError.message("Your VST3 folder can’t be changed by your user account (\(reason)):\n\(destinationRoot.path)\n\nThis usually happens when another installer created the folder as an administrator. In Finder, select the folder, choose File > Get Info, and give your account Read & Write permission, then try again.")
        }
    }
    private func destinationsMatch(_ expected: [(filename: String, hashes: [String: String]?)]) -> Bool {
        for item in expected {
            let destination = destinationRoot.appendingPathComponent(item.filename)
            if let hashes = item.hashes {
                guard Self.isRealDirectory(destination), (try? Self.fileHashes(in: destination)) == hashes else { return false }
            } else if Self.exists(destination) { return false }
        }
        return true
    }
    /// A reason string when an interrupted transaction folder provably holds nothing that is still needed.
    private func provablySafeLeftover(_ folder: URL) -> String? {
        guard Self.isRealDirectory(folder), let contents = try? fm.contentsOfDirectory(atPath: folder.path) else { return nil }
        if contents.isEmpty { return "it was empty" }
        guard let data = try? Data(contentsOf: folder.appendingPathComponent("transaction.json")),
              let journal = try? JSONDecoder().decode(SuiteJournal.self, from: data),
              journal.destination == destinationRoot.path,
              journal.previous.map(\.filename) == SuiteCatalog.plugins.map(\.filename) else { return nil }
        switch journal.state {
        case "staging", "prepared", "rolled-back":
            // Every destination still matches the recorded previous state, so no plug-in was left replaced.
            return destinationsMatch(journal.previous.map { (filename: $0.filename, hashes: $0.hashes) })
                ? "no installed plug-in had been replaced" : nil
        case "all-verified":
            guard let payload = journal.payload else { return nil }
            let expected = SuiteCatalog.plugins.map { (filename: $0.filename, hashes: payload[$0.filename]) }
            guard expected.allSatisfy({ $0.hashes != nil }) else { return nil }
            return destinationsMatch(expected) ? "it had already finished" : nil
        default:
            return nil
        }
    }
    /// Runs under the lock. Finished leftovers are removed; an unfinished transaction is removed only when that is
    /// provably safe, otherwise installation stops with directions.
    private func resolveLeftovers() throws -> [String] {
        var warnings: [String] = []
        let names = try fm.contentsOfDirectory(atPath: destinationRoot.path).sorted()
        for name in names where name.hasPrefix(Self.discardPrefix) {
            if let warning = removeDiscarded(destinationRoot.appendingPathComponent(name)) { warnings.append(warning) }
        }
        for name in names where name.hasPrefix(Self.transactionPrefix) {
            let folder = destinationRoot.appendingPathComponent(name)
            if let reason = provablySafeLeftover(folder) {
                note("Cleaned up an interrupted suite installation (\(reason)): \(name)")
                if let warning = discardTransaction(folder) { warnings.append(warning) }
                continue
            }
            var recorded = ""
            if let data = try? Data(contentsOf: folder.appendingPathComponent("transaction.json")),
               let journal = try? JSONDecoder().decode(SuiteJournal.self, from: data) { recorded = "\n\nRecorded state: \(journal.state)." }
            throw InstallError.message("A previous suite installation was interrupted, and the installer can’t prove that cleaning it up is safe, so nothing was changed.\n\nIts backup copies and a record of what happened are kept in this hidden folder:\n\(folder.path)\n\nIn Finder, open your VST3 folder and press Command-Shift-Period to see it. Please send that folder’s transaction.json file and the installer log to the developer before deleting anything.\(recorded)")
        }
        return warnings
    }
    /// Renames a finished transaction to a discard name first, so a partial delete can never leave a folder that
    /// looks like an unfinished transaction, then deletes it. Returns a warning when the delete fails.
    private func discardTransaction(_ transaction: URL) -> String? {
        guard Self.exists(transaction) else { return nil }
        let discard = destinationRoot.appendingPathComponent(Self.discardPrefix + UUID().uuidString)
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

    func install() throws -> InstallResult {
        log = ["Vivid Fever Dreams Suite \(manifest.version)", "Destination: \(destinationRoot.path)"]
        var warnings: [String] = []
        try checkHosts()
        try Self.refuseSymlinkAncestors(destinationRoot)
        try inspectPayloads()
        note("All five payloads passed identity, SHA-256 and signature checks.")
        try checkDuplicates(cleanUp: false)
        do { try fm.createDirectory(at: destinationRoot, withIntermediateDirectories: true) }
        catch {
            throw InstallError.technical("The installer couldn’t create your VST3 folder:\n\(destinationRoot.path)", detail: "\(error)")
        }
        try Self.refuseSymlinkAncestors(destinationRoot)
        try requireWritableDestination()
        let lock = try Self.acquireLock(in: destinationRoot)
        defer { flock(lock, LOCK_UN); close(lock) }
        warnings += try resolveLeftovers()
        try checkDuplicates(cleanUp: true)
        let previous = try SuiteCatalog.plugins.indices.map { try snapshot($0) }
        let transaction = destinationRoot.appendingPathComponent(Self.transactionPrefix + UUID().uuidString, isDirectory: true)
        try fm.createDirectory(at: transaction, withIntermediateDirectories: false, attributes: [.posixPermissions: 0o700])
        // Extension-less stage names: audio apps scanning the VST3 folder never see staged or retained copies.
        func stageURL(_ index: Int) -> URL { transaction.appendingPathComponent("\(index).staged", isDirectory: true) }
        var state = "staging"
        var items = manifest.plugins.indices.map {
            SuiteJournalItem(filename: manifest.plugins[$0].filename, stage: "\($0).staged", state: "pending")
        }
        var committed: [Int] = []
        let payloadHashes = Dictionary(uniqueKeysWithValues: manifest.plugins.map { ($0.filename, $0.files) })
        func journal() throws {
            let record = SuiteJournal(format: 2, state: state, destination: destinationRoot.path, suiteVersion: manifest.version,
                                      previous: previous, payload: payloadHashes, items: items, committed: committed)
            let encoder = JSONEncoder(); encoder.outputFormatting = [.prettyPrinted, .sortedKeys]
            try encoder.encode(record).write(to: transaction.appendingPathComponent("transaction.json"), options: .atomic)
            journalObserverForTesting?(record)
        }
        do {
            try journal()
            for (index, entry) in manifest.plugins.enumerated() {
                let stage = stageURL(index)
                try fm.copyItem(at: payloadRoot.appendingPathComponent(entry.filename), to: stage)
                try Self.stripQuarantine(in: stage)
                try verifyPayload(at: stage, index: index)
            }
            note("All five plug-ins staged and verified; installed copies are still untouched.")
            try afterStagingForTesting?()
            try checkHosts()
            try checkDuplicates(cleanUp: false)
            for index in previous.indices { try verifyUnchanged(previous[index], index: index) }
            state = "prepared"
            try journal()
            state = "committing"
            for (index, entry) in manifest.plugins.enumerated() {
                try checkHosts()
                try checkDuplicates(cleanUp: false)
                try verifyUnchanged(previous[index], index: index)
                let stage = stageURL(index)
                try verifyPayload(at: stage, index: index)
                let destination = destinationRoot.appendingPathComponent(entry.filename)
                // Record the intent before the rename: after a crash, "committing" means "check by hash".
                items[index].state = "committing"
                try journal()
                let flags = previous[index].identity == nil ? UInt32(RENAME_EXCL) : UInt32(RENAME_SWAP)
                guard renameatx_np(AT_FDCWD, stage.path, AT_FDCWD, destination.path, flags) == 0 else {
                    let code = errno
                    items[index].state = "pending"
                    try? journal()
                    throw InstallError.technical("macOS did not allow the installer to place \(entry.name) in your VST3 folder (\(String(cString: strerror(code)))).",
                                                 detail: "renameatx_np \(stage.path) -> \(destination.path) flags \(flags): errno \(code)")
                }
                committed.append(index)
                items[index].state = "committed"
                try journal()
                try afterCommitForTesting?(committed.count)
                if failAfterCommitForTesting == committed.count { throw InstallError.message("Injected failure after commit \(committed.count)") }
                try Self.refuseSymlinkAncestors(destination)
                try verifyPayload(at: destination, index: index)
                note("Verified \(entry.name) \(entry.identity.version); retaining previous bundles until the suite is complete.")
            }
            for (index, entry) in manifest.plugins.enumerated() {
                let destination = destinationRoot.appendingPathComponent(entry.filename)
                try Self.refuseSymlinkAncestors(destination)
                try verifyPayload(at: destination, index: index)
            }
            state = "all-verified"
            try journal()
            note("All five installed plug-ins verified. Previous transaction copies can now be removed.")
        } catch {
            log.append("Transaction failed: \(UserFacingText.logText(for: error))")
            // An intent recorded just before a rename that never happened is still pending.
            for index in items.indices where items[index].state == "committing" && !committed.contains(index) {
                items[index].state = "pending"
            }
            state = "rolling-back"
            try? journal()
            var rollbackErrors: [String] = []
            for index in committed.reversed() {
                let entry = manifest.plugins[index]
                let destination = destinationRoot.appendingPathComponent(entry.filename)
                let stage = stageURL(index)
                items[index].state = "restoring"
                try? journal()
                do {
                    // Never erase an independently changed destination during recovery.
                    try Self.refuseSymlinkAncestors(destination)
                    guard try Self.fileHashes(in: destination) == entry.files else {
                        throw InstallError.message("The destination changed outside this transaction.")
                    }
                    let old = previous[index]
                    let source = old.identity == nil ? destination : stage
                    let target = old.identity == nil ? stage : destination
                    let flags = old.identity == nil ? UInt32(RENAME_EXCL) : UInt32(RENAME_SWAP)
                    guard renameatx_np(AT_FDCWD, source.path, AT_FDCWD, target.path, flags) == 0 else {
                        throw InstallError.message(String(cString: strerror(errno)))
                    }
                    if let hashes = old.hashes {
                        guard try Self.fileHashes(in: destination) == hashes else { throw InstallError.message("Restored copy failed its integrity check.") }
                    } else if Self.exists(destination) { throw InstallError.message("New installation could not be removed.") }
                    // The stage now holds the new copy that was rolled back. Remove it so retained data is never ambiguous.
                    if Self.exists(stage) { try? fm.removeItem(at: stage) }
                    items[index].state = "restored"
                    try? journal()
                    log.append("Restored previous state of \(entry.name).")
                } catch {
                    items[index].state = "rollback-failed"
                    try? journal()
                    rollbackErrors.append("\(entry.name): \(error.localizedDescription)")
                }
            }
            if !rollbackErrors.isEmpty {
                state = "recovery-required"
                try? journal()
                throw InstallError.message("Installation failed, and the installer could not fully restore your previous plug-ins. Nothing will be deleted. Backup copies and a record of each plug-in’s state are kept in this hidden folder:\n\(transaction.path)\n\n\(rollbackErrors.joined(separator: "\n"))\n\nPlease send the installer log to the developer before changing anything. Original problem: \(UserFacingText.message(for: error))")
            }
            state = "rolled-back"
            try? journal()
            if !committed.isEmpty { note("The complete suite transaction was rolled back; prior installed bundles were restored.") }
            _ = discardTransaction(transaction)
            throw error
        }
        if let warning = discardTransaction(transaction) { warnings.append(warning) }
        note("Saved presets and DAW projects were not modified.")
        let updated = previous.filter { $0.identity != nil }.count
        return InstallResult(installed: previous.count - updated, updated: updated, destination: destinationRoot, log: log, warnings: warnings)
    }
}
