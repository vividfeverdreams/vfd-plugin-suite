import Foundation
import CryptoKit
import Darwin

enum InstallError: LocalizedError {
    case message(String)
    var errorDescription: String? { if case .message(let value) = self { return value }; return nil }
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
}

enum InstallerHostDetection {
    static func isInstaller(_ identifier: String?) -> Bool {
        let id = (identifier ?? "").lowercased()
        return id == SuiteCatalog.installerIdentifier ||
            ["com.yoiboi.synth.installer", "com.yoiboi.installer", "com.yoiboi.throwup.installer",
             "com.yoiboi.backscratch.installer", "com.yoiboi.shifty.installer"].contains(id)
    }
    static func isAudioApplication(bundleIdentifier: String?, name: String?,
                                   processID: Int32, installerProcessID: Int32) -> Bool {
        guard processID != installerProcessID, !isInstaller(bundleIdentifier) else { return false }
        let id = (bundleIdentifier ?? "").lowercased()
        let applicationName = (name ?? "").lowercased()
        let prefixes = ["com.ableton.", "com.apple.logic", "com.apple.garageband", "com.cockos.reaper",
                        "com.steinberg.", "com.bitwig.", "com.presonus.", "com.image-line.",
                        "com.motu.", "com.avid.protools", "com.tracktion."]
        let names = ["ableton live", "reaper", "cubase", "nuendo", "bitwig studio", "studio one",
                     "fl studio", "logic pro", "garageband", "pluginval", "digital performer", "pro tools"]
        return prefixes.contains(where: id.hasPrefix) || names.contains(where: applicationName.hasPrefix)
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
}

/// One five-payload transaction. The shipping UI supplies the user's VST3 folder;
/// every self-test supplies a newly created temporary directory instead.
final class SuiteInstallerEngine {
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

    init(payloadRoot: URL, manifest: SuiteManifest, destinationRoot: URL, systemRoot: URL,
         runningHosts: @escaping () -> [String]) {
        self.payloadRoot = payloadRoot
        self.manifest = manifest
        self.destinationRoot = destinationRoot
        self.systemRoot = systemRoot
        self.runningHosts = runningHosts
    }

    static func readManifest(_ url: URL) throws -> SuiteManifest {
        try JSONDecoder().decode(SuiteManifest.self, from: Data(contentsOf: url))
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
                    throw InstallError.message("Use a normal folder, rather than a symbolic link: \(current.path)")
                }
            } else if errno != ENOENT {
                throw InstallError.message("Cannot inspect \(current.path): \(String(cString: strerror(errno)))")
            }
        }
    }
    static func fileHashes(in bundle: URL) throws -> [String: String] {
        try refuseSymlinkAncestors(bundle)
        var enumerationError: Error?
        guard let enumerator = FileManager.default.enumerator(at: bundle, includingPropertiesForKeys: nil,
            errorHandler: { _, error in enumerationError = error; return false }) else {
            throw InstallError.message("Cannot read \(bundle.lastPathComponent).")
        }
        var hashes: [String: String] = [:]
        for case let url as URL in enumerator {
            var info = stat()
            guard lstat(url.path, &info) == 0 else { throw InstallError.message("Cannot inspect \(url.path).") }
            let type = info.st_mode & S_IFMT
            guard type == S_IFREG || type == S_IFDIR else {
                throw InstallError.message("Unexpected link or special file in \(bundle.lastPathComponent).")
            }
            if type == S_IFREG {
                let relative = String(url.path.dropFirst(bundle.path.count + 1))
                let digest = SHA256.hash(data: try Data(contentsOf: url, options: .mappedIfSafe))
                hashes[relative] = digest.map { String(format: "%02x", $0) }.joined()
            }
        }
        if let error = enumerationError { throw error }
        guard !hashes.isEmpty else { throw InstallError.message("Empty plug-in: \(bundle.path)") }
        return hashes
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
    static func identity(at bundle: URL, definition: PluginDefinition) throws -> PluginIdentity {
        try refuseSymlinkAncestors(bundle)
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
        let expression = try NSRegularExpression(pattern: ",\\s*([}\\]])")
        let normalized = expression.stringByReplacingMatches(in: text,
            range: NSRange(text.startIndex..., in: text), withTemplate: "$1")
        guard let data = normalized.data(using: .utf8),
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
        try Self.run("/usr/bin/codesign", ["--verify", "--deep", "--strict", bundle.path])
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
    private func checkDuplicates() throws {
        for root in [destinationRoot, systemRoot] where Self.exists(root) {
            try Self.refuseSymlinkAncestors(root)
            for candidate in try fm.contentsOfDirectory(at: root, includingPropertiesForKeys: nil)
                where candidate.pathExtension.lowercased() == "vst3" {
                let infoURL = candidate.appendingPathComponent("Contents/Info.plist")
                let object = (try? Data(contentsOf: infoURL)).flatMap { try? PropertyListSerialization.propertyList(from: $0, format: nil) }
                let id = (object as? [String: Any])?["CFBundleIdentifier"] as? String
                for definition in SuiteCatalog.plugins {
                    let sameName = candidate.lastPathComponent.lowercased() == definition.filename.lowercased()
                    let sameID = id == definition.bundleIdentifier
                    if (sameName || sameID) && root == systemRoot {
                        throw InstallError.message("A system-wide copy of \(definition.name) already exists at:\n\(candidate.path)\n\nMove that copy into your user VST3 folder before installing the suite, to avoid duplicate plug-ins.")
                    }
                    if sameID && candidate.lastPathComponent != definition.filename {
                        throw InstallError.message("A renamed copy of \(definition.name) exists at \(candidate.path). Restore its standard name \(definition.filename) before updating.")
                    }
                }
            }
        }
    }
    private struct Snapshot: Codable {
        let filename: String
        let identity: PluginIdentity?
        let hashes: [String: String]?
    }
    private func snapshot(_ index: Int) throws -> Snapshot {
        let definition = SuiteCatalog.plugins[index]
        let entry = manifest.plugins[index]
        let destination = destinationRoot.appendingPathComponent(definition.filename)
        try Self.refuseSymlinkAncestors(destination)
        guard Self.exists(destination) else { return Snapshot(filename: definition.filename, identity: nil, hashes: nil) }
        let identity = try Self.identity(at: destination, definition: definition)
        guard identity.componentID == entry.identity.componentID, identity.controllerID == entry.identity.controllerID else {
            throw InstallError.message("The installed \(definition.name) uses different VST3 identities. It was left untouched to protect existing projects.")
        }
        guard identity.version.compare(entry.identity.version, options: .numeric) != .orderedDescending else {
            throw InstallError.message("\(definition.name) \(identity.version) is newer than this suite’s \(entry.identity.version). No downgrade was performed.")
        }
        return Snapshot(filename: definition.filename, identity: identity, hashes: try Self.fileHashes(in: destination))
    }
    private func verifyUnchanged(_ old: Snapshot, index: Int) throws {
        let current = try snapshot(index)
        guard old.identity == current.identity, old.hashes == current.hashes else {
            throw InstallError.message("\(old.filename) changed while the suite was preparing. Please try again.")
        }
    }
    func install() throws -> InstallResult {
        log = ["Vivid Fever Dreams Suite \(manifest.version)", "Destination: \(destinationRoot.path)"]
        try checkHosts()
        try Self.refuseSymlinkAncestors(destinationRoot)
        try inspectPayloads()
        note("All five payloads passed identity, SHA-256 and signature checks.")
        try checkDuplicates()
        try fm.createDirectory(at: destinationRoot, withIntermediateDirectories: true)
        try Self.refuseSymlinkAncestors(destinationRoot)
        let lockURL = destinationRoot.appendingPathComponent(".vfd-suite-installer.lock")
        let lock = open(lockURL.path, O_CREAT | O_RDWR | O_NOFOLLOW, S_IRUSR | S_IWUSR)
        guard lock >= 0 else { throw InstallError.message("Cannot acquire the suite installation lock.") }
        defer { flock(lock, LOCK_UN); close(lock) }
        var lockInfo = stat()
        guard fstat(lock, &lockInfo) == 0, (lockInfo.st_mode & S_IFMT) == S_IFREG,
              lockInfo.st_nlink == 1, lockInfo.st_uid == geteuid(), flock(lock, LOCK_EX | LOCK_NB) == 0 else {
            throw InstallError.message("Another suite installation is running, or the installation lock is unsafe. Close the other installer and try again.")
        }
        let leftovers = try fm.contentsOfDirectory(atPath: destinationRoot.path).filter { $0.hasPrefix(".vfd-suite-transaction-") }
        guard leftovers.isEmpty else {
            throw InstallError.message("An unfinished suite transaction is preserved in \(destinationRoot.path)/\(leftovers[0]). Review its journal and recover its previous plug-ins before retrying. The installer will not erase it.")
        }
        try checkDuplicates()
        let previous = try SuiteCatalog.plugins.indices.map { try snapshot($0) }
        let transaction = destinationRoot.appendingPathComponent(".vfd-suite-transaction-\(UUID().uuidString)", isDirectory: true)
        try fm.createDirectory(at: transaction, withIntermediateDirectories: false, attributes: [.posixPermissions: 0o700])
        var keepTransaction = false
        var committed: [Int] = []
        defer {
            if !keepTransaction && Self.exists(transaction) {
                do { try fm.removeItem(at: transaction) }
                catch { log.append("Temporary transaction folder remains: \(transaction.path). \(error.localizedDescription)") }
            }
        }
        func journal(_ state: String) throws {
            struct Journal: Codable { let state: String; let destination: String; let previous: [Snapshot]; let committed: [Int] }
            let encoder = JSONEncoder(); encoder.outputFormatting = [.prettyPrinted, .sortedKeys]
            try encoder.encode(Journal(state: state, destination: destinationRoot.path, previous: previous, committed: committed))
                .write(to: transaction.appendingPathComponent("transaction.json"), options: .atomic)
        }
        do {
            try journal("staging")
            for (index, entry) in manifest.plugins.enumerated() {
                let stage = transaction.appendingPathComponent(entry.filename)
                try fm.copyItem(at: payloadRoot.appendingPathComponent(entry.filename), to: stage)
                try verifyPayload(at: stage, index: index)
            }
            note("All five plug-ins staged and verified; installed copies are still untouched.")
            try afterStagingForTesting?()
            try checkHosts()
            try checkDuplicates()
            for index in previous.indices { try verifyUnchanged(previous[index], index: index) }
            try journal("prepared")
            for (index, entry) in manifest.plugins.enumerated() {
                try checkHosts()
                try checkDuplicates()
                try verifyUnchanged(previous[index], index: index)
                let stage = transaction.appendingPathComponent(entry.filename)
                try verifyPayload(at: stage, index: index)
                let destination = destinationRoot.appendingPathComponent(entry.filename)
                let flags = previous[index].identity == nil ? UInt32(RENAME_EXCL) : UInt32(RENAME_SWAP)
                guard renameatx_np(AT_FDCWD, stage.path, AT_FDCWD, destination.path, flags) == 0 else {
                    throw InstallError.message("Could not commit \(entry.name): \(String(cString: strerror(errno)))")
                }
                committed.append(index)
                try journal("committing")
                if failAfterCommitForTesting == committed.count { throw InstallError.message("Injected failure after commit \(committed.count)") }
                try verifyPayload(at: destination, index: index)
                note("Verified \(entry.name) \(entry.identity.version); retaining previous bundles until the suite is complete.")
            }
            for (index, entry) in manifest.plugins.enumerated() {
                try verifyPayload(at: destinationRoot.appendingPathComponent(entry.filename), index: index)
            }
            try journal("all-verified")
            note("All five installed plug-ins verified. Previous transaction copies can now be removed.")
        } catch {
            log.append("Transaction failed: \(error.localizedDescription)")
            var rollbackErrors: [String] = []
            for index in committed.reversed() {
                let entry = manifest.plugins[index]
                let destination = destinationRoot.appendingPathComponent(entry.filename)
                let stage = transaction.appendingPathComponent(entry.filename)
                do {
                    // Never erase an independently changed destination during recovery.
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
                    log.append("Restored previous state of \(entry.name).")
                } catch { rollbackErrors.append("\(entry.name): \(error.localizedDescription)") }
            }
            if !rollbackErrors.isEmpty {
                keepTransaction = true
                try? journal("recovery-required")
                throw InstallError.message("Installation failed and recovery needs attention. Previous bundles and the journal are preserved at:\n\(transaction.path)\n\n\(rollbackErrors.joined(separator: "\n"))\nOriginal error: \(error.localizedDescription)")
            }
            if !committed.isEmpty { note("The complete suite transaction was rolled back; prior installed bundles were restored.") }
            throw error
        }
        note("Saved presets and DAW projects were not modified.")
        let updated = previous.filter { $0.identity != nil }.count
        return InstallResult(installed: previous.count - updated, updated: updated, destination: destinationRoot, log: log)
    }
}
