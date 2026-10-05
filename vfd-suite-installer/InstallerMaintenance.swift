import Foundation
import Darwin

struct UninstallResult {
    let removed: [(key: ItemKey, trashedAt: URL?)]
    let notInstalled: [ItemKey]
    let audioUnitsRefreshed: Bool
    let log: [String]
}

/// Something Repair… can fix: an interrupted suite transaction, a finished one that could not be deleted, a temporary
/// copy left by a single-plugin installer, or a previous copy a single-plugin installer saved to the Recovered folder.
struct RepairIssue: Equatable {
    enum Kind: Equatable {
        case suiteTransaction(state: String, safe: Bool)
        case finishedTransaction
        case singlePluginStage
        case recoveredCopy(ItemKey)
    }
    let url: URL
    let kind: Kind
    let title: String
    let detail: String
    let canRestore: Bool
}

extension SuiteInstallerEngine {
    /// macOS caches Audio Unit registrations in the per-user AudioComponentRegistrar agent. Ending it — the approach
    /// plug-in installers commonly use — makes macOS rebuild that cache the next time an app asks, and launchd starts
    /// the agent again on demand. Apple does not document this, so it is best effort: a plain SIGTERM (not -9),
    /// limited to the current user's process.
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

    func itemKey(forFilename name: String) -> ItemKey? { SuiteCatalog.allItems.first { $0.filename == name } }

    // MARK: Uninstall

    /// Moves the selected, verified VFD bundles to the Trash. Every item is verified (bundle identifier plus VST3
    /// class IDs or AU codes) before anything moves, so an unexpected item stops the whole request.
    func uninstall(selection: Set<ItemKey>) throws -> UninstallResult {
        let keys = selection.sorted()
        guard !keys.isEmpty else { throw InstallError.message("Select at least one plug-in format to remove.") }
        let formats = Set(keys.map(\.format))
        log = ["Uninstall: \(keys.map(\.title).joined(separator: ", "))"]
        try checkHosts(formats)
        let locked = try lockFolders(create: [])
        defer { Self.unlock(locked.locks) }
        var verified: [ItemKey] = []
        var notInstalled: [ItemKey] = []
        var problems: [String] = []
        for key in keys {
            let destination = destinationURL(key)
            do {
                try Self.refuseSymlinkAncestors(destination)
                guard Self.exists(destination) else { notInstalled.append(key); continue }
                let identity = try Self.identity(at: destination, definition: key.definition, format: key.format)
                guard Self.sameProduct(identity, manifest.entry(key).identity, format: key.format, definition: key.definition) else {
                    throw InstallError.message("it uses different identities")
                }
                verified.append(key)
            } catch {
                problems.append("\(key.title): \(UserFacingText.message(for: error))")
            }
        }
        guard problems.isEmpty else {
            throw InstallError.message("Nothing was moved to the Trash, because these items could not be confirmed as Vivid Fever Dreams plug-ins:\n\(problems.joined(separator: "\n"))")
        }
        var removed: [(key: ItemKey, trashedAt: URL?)] = []
        for key in verified {
            do {
                removed.append((key, try moveToTrash(destinationURL(key))))
                note("Moved \(key.title) to the Trash.")
            } catch {
                let done = removed.isEmpty ? "Nothing was removed." : "Already in the Trash: \(removed.map { $0.key.title }.joined(separator: ", "))."
                throw InstallError.message("\(key.title) could not be moved to the Trash (\(error.localizedDescription)). \(done)")
            }
        }
        var refreshed = false
        if verified.contains(where: { $0.format == .au }) { note(refreshAudioUnits()); refreshed = true }
        return UninstallResult(removed: removed, notInstalled: notInstalled, audioUnitsRefreshed: refreshed, log: log)
    }

    // MARK: Repair

    enum RestoreStep {
        case swapBack(stage: URL, destination: URL, expected: [String: String])
        case moveBack(stage: URL, destination: URL, expected: [String: String])
        case trashNew(destination: URL)
        var destination: URL {
            switch self {
            case .swapBack(_, let destination, _), .moveBack(_, let destination, _), .trashNew(let destination): return destination
            }
        }
    }

    func repairIssues() -> [RepairIssue] {
        var issues: [RepairIssue] = []
        for format in PluginFormat.allCases.sorted() {
            let root = locations.destinationRoot(format)
            guard Self.isRealDirectory(root), let names = try? fm.contentsOfDirectory(atPath: root.path) else { continue }
            for name in names.sorted() {
                let url = root.appendingPathComponent(name)
                if name.hasPrefix(Self.transactionPrefix) {
                    let state = Self.readJournal(url)?.state ?? "unknown"
                    let safe = provablySafeLeftover(url, root: root) != nil
                    var restorable = false
                    var detail = safe ? "Nothing needs restoring. It can be cleaned up safely."
                        : "Recorded state: \(state). It holds backup copies of plug-ins that were being replaced."
                    if !safe {
                        do { restorable = !(try restorePlan(url, root: root)).isEmpty }
                        catch { detail += " Automatic restore is not possible: \(UserFacingText.message(for: error))" }
                    }
                    issues.append(RepairIssue(url: url, kind: .suiteTransaction(state: state, safe: safe), title: "Interrupted suite installation",
                                              detail: detail, canRestore: restorable))
                } else if name.hasPrefix(Self.discardPrefix) {
                    issues.append(RepairIssue(url: url, kind: .finishedTransaction, title: "Finished installation leftovers",
                                              detail: "Copies that are no longer needed. It can be cleaned up safely.", canRestore: false))
                } else if name.range(of: Self.singlePluginStagePattern, options: .regularExpression) != nil {
                    issues.append(RepairIssue(url: url, kind: .singlePluginStage, title: "Temporary copy from a single-plugin update",
                                              detail: "Left behind by an interrupted update. It is not your installed plug-in.", canRestore: false))
                }
            }
        }
        if let saved = try? fm.contentsOfDirectory(at: locations.recovered, includingPropertiesForKeys: nil) {
            for url in saved.sorted(by: { $0.lastPathComponent < $1.lastPathComponent }) {
                guard let key = recoveredKey(url) else { continue }
                issues.append(RepairIssue(url: url, kind: .recoveredCopy(key), title: "Saved previous \(key.title)",
                                          detail: "A single-plugin installer saved this copy when an update could not be undone: \(url.lastPathComponent)",
                                          canRestore: true))
            }
        }
        return issues
    }
    /// The plug-in and format of a copy in the Recovered folder, when it verifies as a Vivid Fever Dreams bundle.
    func recoveredKey(_ url: URL) -> ItemKey? {
        let summary = Self.bundleSummary(at: url)
        guard let format = PluginFormat.allCases.first(where: { $0.fileExtension == url.pathExtension.lowercased() }),
              let plugin = SuiteCatalog.plugins.firstIndex(where: { $0.bundleIdentifier == summary.identifier }) else { return nil }
        let key = ItemKey(plugin: plugin, format: format)
        guard let identity = try? Self.identity(at: url, definition: key.definition, format: format),
              Self.sameProduct(identity, manifest.entry(key).identity, format: format, definition: key.definition) else { return nil }
        return key
    }
    /// Every step needed to put the journal's previous state back, each one checked against recorded SHA-256 hashes.
    /// Throws when any item cannot be restored safely, so a restore is never partial by design.
    func restorePlan(_ folder: URL, root: URL) throws -> [RestoreStep] {
        guard let journal = Self.readJournal(folder), let targets = journalTargets(journal, root: root) else {
            throw InstallError.message("Its record (transaction.json) is missing or unreadable.")
        }
        var steps: [RestoreStep] = []
        for target in targets {
            if matches(target.destination, target.previous) { continue }
            let stage = folder.appendingPathComponent(target.stage.isEmpty ? target.filename : target.stage)
            if let previous = target.previous {
                guard Self.isRealDirectory(stage), (try? Self.fileHashes(in: stage)) == previous else {
                    throw InstallError.message("The backup of \(target.filename) is missing or does not match its recorded checksums.")
                }
                if Self.exists(target.destination) {
                    try requireTransactionCopy(target)
                    steps.append(.swapBack(stage: stage, destination: target.destination, expected: previous))
                } else {
                    steps.append(.moveBack(stage: stage, destination: target.destination, expected: previous))
                }
            } else {
                // The plug-in was not installed before: restoring means removing the copy the transaction added.
                try requireTransactionCopy(target)
                steps.append(.trashNew(destination: target.destination))
            }
        }
        return steps
    }
    /// The bundle now at the destination must be the transaction's own copy, never something changed since.
    private func requireTransactionCopy(_ target: JournalTarget) throws {
        try Self.refuseSymlinkAncestors(target.destination)
        if let payload = target.payload {
            guard (try? Self.fileHashes(in: target.destination)) == payload else {
                throw InstallError.message("\(target.filename) was changed after the interrupted installation, so it was left as it is.")
            }
            return
        }
        // Journals from suite 1.2.2 did not record payload hashes: require at least a verified VFD bundle.
        guard let key = itemKey(forFilename: target.filename),
              (try? Self.identity(at: target.destination, definition: key.definition, format: key.format)) != nil else {
            throw InstallError.message("\(target.filename) is not a Vivid Fever Dreams plug-in, so it was left as it is.")
        }
    }
    private func formats(of urls: [URL]) -> Set<PluginFormat> {
        Set(urls.compactMap { url in PluginFormat.allCases.first { $0.fileExtension == url.pathExtension.lowercased() } })
    }

    /// Puts back the previous versions recorded by an interrupted suite transaction, or a copy saved to Recovered.
    func restorePrevious(_ issue: RepairIssue) throws -> [String] {
        log = ["Repair: restore previous versions from \(issue.url.path)"]
        switch issue.kind {
        case .suiteTransaction:
            let root = issue.url.deletingLastPathComponent()
            let preliminary = try restorePlan(issue.url, root: root)
            try checkHosts(formats(of: preliminary.map(\.destination)))
            let locked = try lockFolders(create: [])
            defer { Self.unlock(locked.locks) }
            let steps = try restorePlan(issue.url, root: root)
            for step in steps {
                switch step {
                case .swapBack(let stage, let destination, let expected):
                    guard renameatx_np(AT_FDCWD, stage.path, AT_FDCWD, destination.path, UInt32(RENAME_SWAP)) == 0 else {
                        throw InstallError.message("Could not restore \(destination.lastPathComponent): \(String(cString: strerror(errno))).")
                    }
                    guard (try? Self.fileHashes(in: destination)) == expected else {
                        throw InstallError.message("The restored \(destination.lastPathComponent) failed its checksum test. The repair folder was kept.")
                    }
                case .moveBack(let stage, let destination, let expected):
                    try fm.createDirectory(at: destination.deletingLastPathComponent(), withIntermediateDirectories: true)
                    guard renameatx_np(AT_FDCWD, stage.path, AT_FDCWD, destination.path, UInt32(RENAME_EXCL)) == 0 else {
                        throw InstallError.message("Could not restore \(destination.lastPathComponent): \(String(cString: strerror(errno))).")
                    }
                    guard (try? Self.fileHashes(in: destination)) == expected else {
                        throw InstallError.message("The restored \(destination.lastPathComponent) failed its checksum test. The repair folder was kept.")
                    }
                case .trashNew(let destination):
                    _ = try moveToTrash(destination)
                }
                note("Restored the previous state of \(step.destination.lastPathComponent).")
            }
            _ = try moveToTrash(issue.url)
            note("Moved the finished repair folder to the Trash.")
            if formats(of: steps.map(\.destination)).contains(.au) { note(refreshAudioUnits()) }
        case .recoveredCopy(let key):
            try checkHosts([key.format])
            let locked = try lockFolders(create: [key.format])
            defer { Self.unlock(locked.locks) }
            guard recoveredKey(issue.url) == key else { throw InstallError.message("The saved copy no longer verifies as \(key.title).") }
            let hashes = try Self.fileHashes(in: issue.url)
            let destination = destinationURL(key)
            try Self.refuseSymlinkAncestors(destination)
            if Self.exists(destination) {
                _ = try Self.identity(at: destination, definition: key.definition, format: key.format)
                _ = try moveToTrash(destination)
                note("Moved the current \(key.title) to the Trash.")
            }
            try fm.moveItem(at: issue.url, to: destination)
            guard try Self.fileHashes(in: destination) == hashes else {
                throw InstallError.message("The restored \(key.title) failed its checksum test.")
            }
            note("Restored the saved \(key.title).")
            if key.format == .au { note(refreshAudioUnits()) }
        case .finishedTransaction, .singlePluginStage:
            throw InstallError.message("This item can only be cleaned up.")
        }
        return log
    }

    /// Moves a leftover to the Trash. Nothing is deleted, so a mistaken clean-up can be undone from the Trash.
    func cleanUp(_ issue: RepairIssue) throws -> URL? {
        log = ["Repair: clean up \(issue.url.path)"]
        let locked = try lockFolders(create: [])
        defer { Self.unlock(locked.locks) }
        guard Self.exists(issue.url) else { return nil }
        let trashed = try moveToTrash(issue.url)
        note("Moved \(issue.url.lastPathComponent) to the Trash.")
        return trashed
    }
}
