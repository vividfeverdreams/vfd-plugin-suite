import Foundation

// Shared by the suite installer and the five single-plugin installers (copied into each Installer folder).
// It depends only on InstallError and PluginFormat, which every installer module defines.

/// Plain-English rescan directions for the audio apps installed on this Mac.
enum RescanGuidance {
    struct Host {
        let name: String
        let bundleIdentifiers: [String]
        let directions: (Set<PluginFormat>) -> String
    }
    static let hosts: [Host] = [
        Host(name: "Ableton Live", bundleIdentifiers: ["com.ableton.live"]) { formats in
            var switches: [String] = []
            if formats.contains(.vst3) { switches.append("“Use VST3 Plug-In System Folders”") }
            if formats.contains(.au) { switches.append("“Use Audio Units v2”") }
            return "Ableton Live: open Settings → Plug-Ins, turn on \(switches.joined(separator: " and ")), then click Rescan. Option-click Rescan for a full rescan, which also refreshes the VIVID FEVER DREAMS creator name."
        },
        Host(name: "Bitwig Studio", bundleIdentifiers: ["com.bitwig.BitwigStudio"]) { formats in
            formats.contains(.vst3)
                ? "Bitwig Studio: restart Bitwig; it scans new VST3 plug-ins when it starts. If they’re missing, check Settings → Locations."
                : "Bitwig Studio doesn’t load Audio Units. Tick VST3 to use these plug-ins in Bitwig."
        },
        Host(name: "FL Studio", bundleIdentifiers: ["com.image-line.flstudio"]) { _ in
            "FL Studio: open Options → Manage plugins and click Find installed plugins (tick Verify plugins to refresh their names)."
        },
        Host(name: "REAPER", bundleIdentifiers: ["com.cockos.reaper"]) { formats in
            var parts: [String] = []
            if formats.contains(.vst3) { parts.append("open Settings → Plug-ins → VST and click Re-scan") }
            if formats.contains(.au) { parts.append("restart REAPER to load the new Audio Units") }
            return "REAPER: " + parts.joined(separator: "; ") + "."
        },
        Host(name: "Logic Pro", bundleIdentifiers: ["com.apple.logic10"]) { formats in
            formats.contains(.au)
                ? "Logic Pro: quit and reopen Logic. If a plug-in is missing, choose Logic Pro → Settings → Plug-in Manager, select it, and click Reset & Rescan Selection."
                : "Logic Pro loads Audio Units only. Tick AU to use these plug-ins in Logic."
        },
        Host(name: "GarageBand", bundleIdentifiers: ["com.apple.garageband10"]) { formats in
            formats.contains(.au)
                ? "GarageBand: quit and reopen GarageBand; it checks new Audio Units when it starts."
                : "GarageBand loads Audio Units only. Tick AU to use these plug-ins in GarageBand."
        },
        Host(name: "MainStage", bundleIdentifiers: ["com.apple.mainstage3"]) { formats in
            formats.contains(.au)
                ? "MainStage: quit and reopen MainStage to load the new Audio Units."
                : "MainStage loads Audio Units only. Tick AU to use these plug-ins in MainStage."
        }
    ]
    /// Directions for the hosts in `installedBundleIdentifiers` (compared case-insensitively), given what was installed.
    static func steps(installedBundleIdentifiers: Set<String>, formats: Set<PluginFormat>) -> [String] {
        let installed = Set(installedBundleIdentifiers.map { $0.lowercased() })
        let found = hosts.filter { host in host.bundleIdentifiers.contains { installed.contains($0.lowercased()) } }
        guard !formats.isEmpty else { return [] }
        if found.isEmpty {
            let what = formats.sorted().map { $0 == .vst3 ? "VST3 plug-ins" : "Audio Units" }.joined(separator: " and ")
            return ["Open your audio app and rescan its \(what)."]
        }
        return found.map { $0.directions(formats) }
    }
    static var allBundleIdentifiers: [String] { hosts.flatMap(\.bundleIdentifiers) }
}

/// The short per-plug-in “What’s new” notes bundled as whats-new.json at build time:
/// a JSON object keyed by plug-in name, each value an array of one to six short lines.
enum WhatsNew {
    static let maximumLines = 6
    static let maximumLength = 160
    static func parse(_ data: Data, requiredNames: [String], allowOtherNames: Bool) throws -> [String: [String]] {
        guard let object = (try? JSONSerialization.jsonObject(with: data)) as? [String: Any] else {
            throw InstallError.message("What’s new must be a JSON object keyed by plug-in name.")
        }
        var notes: [String: [String]] = [:]
        for (name, value) in object {
            guard requiredNames.contains(name) || allowOtherNames else {
                throw InstallError.message("What’s new names an unknown plug-in: \(name).")
            }
            guard let lines = value as? [String], (1...maximumLines).contains(lines.count),
                  lines.allSatisfy({ !$0.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty && $0.count <= maximumLength }) else {
                throw InstallError.message("What’s new for \(name) must be 1 to \(maximumLines) non-empty lines of at most \(maximumLength) characters.")
            }
            notes[name] = lines
        }
        for name in requiredNames where notes[name] == nil {
            throw InstallError.message("What’s new has no entry for \(name).")
        }
        return notes
    }
}
