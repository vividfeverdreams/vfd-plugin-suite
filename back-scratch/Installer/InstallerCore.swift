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

enum InstallerHostDetection {
    static let installerIdentifier = "com.yoiboi.backscratch.installer"

    static func isAudioApplication(bundleIdentifier: String?, name: String?,
                                   processID: Int32, installerProcessID: Int32) -> Bool {
        guard processID != installerProcessID else { return false }
        let id = (bundleIdentifier ?? "").lowercased()
        guard id != installerIdentifier else { return false }
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

/// The engine receives explicit paths so its entire transaction can be exercised in a temporary directory.
/// The shipping UI always supplies the standard per-user VST3 location.
final class InstallerEngine {
    static let identifier = "com.yoiboi.backscratch"
    static let executable = "BACK-SCRATCH"
    private let fm = FileManager.default
    let payload: URL
    let manifest: PayloadManifest
    let destination: URL
    let systemCopy: URL
    let runningHosts: () -> [String]
    var log: [String] = []
    // Test fault injection never appears in the app's command line or UI.
    var failAfterCommitForTesting = false

    init(payload: URL, manifest: PayloadManifest, destination: URL, systemCopy: URL,
         runningHosts: @escaping () -> [String]) {
        self.payload = payload
        self.manifest = manifest
        self.destination = destination
        self.systemCopy = systemCopy
        self.runningHosts = runningHosts
    }

    static func readManifest(_ url: URL) throws -> PayloadManifest {
        try JSONDecoder().decode(PayloadManifest.self, from: Data(contentsOf: url))
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
                    throw InstallError.message("Installation stopped because this path is a symbolic link: \(current.path). Use a normal folder for BACK-SCRATCH.")
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
            throw InstallError.message("The BACK-SCRATCH payload cannot be read.")
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
        guard !hashes.isEmpty else { throw InstallError.message("The BACK-SCRATCH payload is empty.") }
        return hashes
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
            throw InstallError.message("The existing item is not the expected BACK-SCRATCH plug-in. It has been left untouched.")
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
            throw InstallError.message("The VST3 identity does not match BACK-SCRATCH. No installed plug-in was changed.")
        }
        return PluginIdentity(version: version, componentID: component.uppercased(),
                              controllerID: controller.uppercased())
    }

    func verifyPayload(at bundle: URL) throws {
        guard manifest.format == 1, manifest.bundleIdentifier == Self.identifier else {
            throw InstallError.message("The installer manifest does not describe this BACK-SCRATCH plug-in.")
        }
        let identity = try Self.identity(at: bundle)
        guard identity.version == manifest.version,
              identity.componentID == manifest.componentID,
              identity.controllerID == manifest.controllerID else {
            throw InstallError.message("The plug-in identity does not match the installer.")
        }
        guard fm.isExecutableFile(atPath: bundle.appendingPathComponent("Contents/MacOS/BACK-SCRATCH").path) else {
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

    func install() throws -> InstallResult {
        log = ["BACK-SCRATCH installer \(manifest.version)", "Destination: \(destination.path)"]
        try checkHosts()
        try Self.refuseSymlinkAncestors(destination)
        guard destination.lastPathComponent == "BACK-SCRATCH.vst3" else {
            throw InstallError.message("The destination must be named BACK-SCRATCH.vst3.")
        }
        guard !Self.exists(systemCopy) else {
            throw InstallError.message("BACK-SCRATCH is already in the system VST3 folder:\n\(systemCopy.path)\n\nTo avoid duplicate entries, move that BACK-SCRATCH.vst3 into your user VST3 folder first, then run this installer again:\n\(destination.deletingLastPathComponent().path)")
        }
        try verifyPayload(at: payload)
        log.append("Embedded payload: SHA-256, bundle identity, VST3 identities and code signature verified.")
        let updated = Self.exists(destination)
        var previousIdentity: PluginIdentity?
        var previousHashes: [String: String]?
        if updated {
            let oldIdentity = try Self.identity(at: destination)
            guard oldIdentity.componentID == manifest.componentID,
                  oldIdentity.controllerID == manifest.controllerID else {
                throw InstallError.message("This BACK-SCRATCH copy uses different VST3 identities. It has been left untouched to protect existing projects.")
            }
            let oldVersion = oldIdentity.version
            guard oldVersion.compare(manifest.version, options: .numeric) != .orderedDescending else {
                throw InstallError.message("BACK-SCRATCH \(oldVersion) is already installed. This installer contains the older version \(manifest.version), so no changes were made.")
            }
            // Refuse links anywhere in an installed bundle before replacing it.
            previousIdentity = oldIdentity
            previousHashes = try Self.fileHashes(in: destination)
            log.append("Updating existing version \(oldVersion).")
        }
        let parent = destination.deletingLastPathComponent()
        try fm.createDirectory(at: parent, withIntermediateDirectories: true)
        try Self.refuseSymlinkAncestors(parent)
        let stage = parent.appendingPathComponent(".backscratch-stage-\(UUID().uuidString).vst3", isDirectory: true)
        var ownsStage = false
        var committed = false
        defer {
            if ownsStage && Self.exists(stage) {
                do { try fm.removeItem(at: stage) }
                catch { log.append("Temporary transaction folder remains at \(stage.path): \(error.localizedDescription)") }
            }
        }
        try fm.copyItem(at: payload, to: stage)
        ownsStage = true
        try verifyPayload(at: stage)
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
            throw InstallError.message("Unable to replace BACK-SCRATCH: \(String(cString: strerror(errno))). The previous version remains in place.")
        }
        committed = true
        do {
            if failAfterCommitForTesting { throw InstallError.message("Injected verification failure") }
            try verifyPayload(at: destination)
            log.append(updated ? "Atomic replacement completed and verified." : "Installation completed and verified.")
        } catch {
            if committed {
                if updated {
                    guard renameatx_np(AT_FDCWD, stage.path, AT_FDCWD, destination.path, UInt32(RENAME_SWAP)) == 0 else {
                        ownsStage = false
                        throw InstallError.message("Verification failed and automatic rollback could not finish. Your previous plug-in is preserved at \(stage.path). Details: \(error.localizedDescription)")
                    }
                    log.append("Verification failed; previous plug-in restored atomically.")
                } else {
                    guard renameatx_np(AT_FDCWD, destination.path, AT_FDCWD, stage.path, UInt32(RENAME_EXCL)) == 0 else {
                        throw InstallError.message("Verification failed. Could not remove the new plug-in at \(destination.path): \(error.localizedDescription)")
                    }
                    log.append("Verification failed; initial installation rolled back.")
                }
            }
            throw error
        }
        log.append("Saved presets and Live projects were not modified.")
        return InstallResult(updated: updated, version: manifest.version, destination: destination, log: log)
    }
}
