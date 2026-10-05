import AppKit

final class InstallerDelegate: NSObject, NSApplicationDelegate, NSWindowDelegate {
    private var window: NSWindow!
    private let action = NSButton(title: "Install / Update", target: nil, action: nil)
    private let status = NSTextField(wrappingLabelWithString: "Quit Ableton Live and any other audio app before installing.")
    private let progress = NSProgressIndicator()
    private var busy = false
    private let vst3Box = NSButton(checkboxWithTitle: "VST3  →  ~/Library/Audio/Plug-Ins/VST3", target: nil, action: nil)
    private let auBox = NSButton(checkboxWithTitle: "Audio Unit for Logic Pro and GarageBand  →  ~/Library/Audio/Plug-Ins/Components",
                                 target: nil, action: nil)
    private let whatsNewLabel = NSTextField(wrappingLabelWithString: "")

    func applicationDidFinishLaunching(_ notification: Notification) {
        installMainMenu()
        window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 590, height: 490),
                          styleMask: [.titled, .closable, .miniaturizable], backing: .buffered, defer: false)
        window.title = "BACK-SCRATCH Installer"
        window.isReleasedWhenClosed = false
        window.delegate = self
        window.center()
        let content = NSView(frame: window.contentView!.bounds)
        content.wantsLayer = true
        content.layer?.backgroundColor = NSColor(calibratedRed: 0.12, green: 0.025, blue: 0.045, alpha: 1).cgColor
        window.contentView = content
        let logo = label("BACK-SCRATCH", size: 45, weight: .black, color: .white)
        logo.font = NSFont(descriptor: logo.font!.fontDescriptor.withSymbolicTraits(.italic), size: 45)
        logo.frame = NSRect(x: 34, y: 403, width: 520, height: 59)
        content.addSubview(logo)
        let version = Bundle.main.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String ?? ""
        let build = Bundle.main.object(forInfoDictionaryKey: "CFBundleVersion") as? String ?? version
        let suffix = build == version ? "" : "  •  UPDATED INSTALLER"
        let subtitle = label("SCRATCH AUTOMATION  •  VERSION \(version)\(suffix)", size: 12, weight: .semibold,
                             color: NSColor(calibratedRed: 1.0, green: 0.43, blue: 0.48, alpha: 1))
        subtitle.frame = NSRect(x: 36, y: 377, width: 520, height: 22)
        content.addSubview(subtitle)
        let explanation = NSTextField(wrappingLabelWithString:
            "Install BACK-SCRATCH as a VST3 and an Audio Unit, or update the copy already there. Existing Ableton projects stay in place.")
        explanation.font = .systemFont(ofSize: 14)
        explanation.textColor = NSColor(white: 0.94, alpha: 1)
        explanation.frame = NSRect(x: 36, y: 296, width: 518, height: 72)
        content.addSubview(explanation)
        let accent = NSColor(calibratedRed: 0.91, green: 0.72, blue: 0.72, alpha: 1)
        whatsNewLabel.frame = NSRect(x: 36, y: 262, width: 518, height: 30)
        whatsNewLabel.font = .systemFont(ofSize: 11)
        whatsNewLabel.textColor = accent
        whatsNewLabel.stringValue = Self.whatsNewText()
        whatsNewLabel.cell?.truncatesLastVisibleLine = true
        content.addSubview(whatsNewLabel)
        for (box, y) in [(vst3Box, CGFloat(236)), (auBox, CGFloat(212))] {
            box.frame = NSRect(x: 34, y: y, width: 522, height: 20)
            box.state = .on
            box.attributedTitle = NSAttributedString(string: box.title,
                attributes: [.foregroundColor: accent, .font: NSFont.systemFont(ofSize: 12, weight: .medium)])
            content.addSubview(box)
        }
        status.font = .systemFont(ofSize: 14)
        status.textColor = NSColor(calibratedRed: 1.0, green: 0.84, blue: 0.82, alpha: 1)
        status.frame = NSRect(x: 36, y: 104, width: 518, height: 100)
        content.addSubview(status)
        action.target = self
        action.action = #selector(install)
        action.bezelStyle = .rounded
        action.controlSize = .large
        action.font = .systemFont(ofSize: 16, weight: .bold)
        action.keyEquivalent = "\r"
        action.frame = NSRect(x: 350, y: 42, width: 206, height: 45)
        content.addSubview(action)
        let close = NSButton(title: "Close", target: self, action: #selector(closeWindow))
        close.bezelStyle = .rounded
        close.frame = NSRect(x: 30, y: 44, width: 88, height: 36)
        content.addSubview(close)
        progress.style = .spinning
        progress.isDisplayedWhenStopped = false
        progress.frame = NSRect(x: 317, y: 54, width: 20, height: 20)
        content.addSubview(progress)
        window.makeKeyAndOrderFront(nil)
        NSApplication.shared.activate(ignoringOtherApps: true)
    }

    private func installMainMenu() {
        let mainMenu = NSMenu()
        let appItem = NSMenuItem()
        let appMenu = NSMenu()
        appMenu.addItem(withTitle: "Quit Install BACK-SCRATCH", action: #selector(NSApplication.terminate(_:)), keyEquivalent: "q")
        appItem.submenu = appMenu
        mainMenu.addItem(appItem)
        let fileItem = NSMenuItem()
        let fileMenu = NSMenu(title: "File")
        fileMenu.addItem(withTitle: "Close", action: #selector(NSWindow.performClose(_:)), keyEquivalent: "w")
        fileItem.submenu = fileMenu
        mainMenu.addItem(fileItem)
        let editItem = NSMenuItem()
        let editMenu = NSMenu(title: "Edit")
        editMenu.addItem(withTitle: "Copy", action: #selector(NSText.copy(_:)), keyEquivalent: "c")
        editMenu.addItem(withTitle: "Select All", action: #selector(NSText.selectAll(_:)), keyEquivalent: "a")
        editItem.submenu = editMenu
        mainMenu.addItem(editItem)
        NSApplication.shared.mainMenu = mainMenu
    }

    private func label(_ text: String, size: CGFloat, weight: NSFont.Weight, color: NSColor) -> NSTextField {
        let result = NSTextField(labelWithString: text)
        result.font = .systemFont(ofSize: size, weight: weight)
        result.textColor = color
        return result
    }

    static func runningHosts(formats: Set<PluginFormat>) -> [String] {
        let ownPID = ProcessInfo.processInfo.processIdentifier
        let applications = NSWorkspace.shared.runningApplications
        var excludedPIDs: Set<Int32> = [ownPID]
        for app in applications where InstallerHostDetection.isInstaller(app.bundleIdentifier) {
            excludedPIDs.insert(app.processIdentifier)
        }
        var found = applications.filter { app in
            InstallerHostDetection.isAudioApplication(bundleIdentifier: app.bundleIdentifier,
                name: app.localizedName, processID: app.processIdentifier, installerProcessID: ownPID, formats: formats)
        }.map { $0.localizedName ?? $0.bundleIdentifier ?? "Audio application" }
        // Covers hosts we do not recognize by application name, without changing or terminating them.
        let locations = InstallLocations.standard
        let executables = formats.map {
            locations.destinationRoot($0).appendingPathComponent("BACK-SCRATCH.\($0.fileExtension)/Contents/MacOS/BACK-SCRATCH")
        }.filter { InstallerEngine.exists($0) }.map(\.path)
        found.append(contentsOf: InstallerHostDetection.openFileUsers(paths: executables, excluding: excludedPIDs))
        return Array(Set(found))
    }

    /// This release's What's new notes, shown above the format checkboxes.
    static func whatsNewText() -> String {
        guard let url = Bundle.main.url(forResource: "whats-new", withExtension: "json"), let data = try? Data(contentsOf: url),
              let notes = try? WhatsNew.parse(data, requiredNames: ["BACK-SCRATCH"], allowOtherNames: true), let lines = notes["BACK-SCRATCH"] else { return "" }
        let version = Bundle.main.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String ?? ""
        return "What’s new in \(version): " + lines.joined(separator: " · ")
    }

    /// Rescan directions for the audio apps installed on this Mac.
    static func nextSteps(_ result: InstallResult) -> [String] {
        let hosts = Set(RescanGuidance.allBundleIdentifiers.filter { NSWorkspace.shared.urlForApplication(withBundleIdentifier: $0) != nil })
        var steps = RescanGuidance.steps(installedBundleIdentifiers: hosts, formats: Set(result.installed + result.updated + result.skipped))
        if result.audioUnitsRefreshed {
            steps.append("macOS was asked to refresh its Audio Unit list. Restart Logic Pro, GarageBand or MainStage before looking for the new Audio Unit.")
        }
        return steps
    }

    @objc private func install() {
        guard !busy else { return }
        var formats = Set<PluginFormat>()
        if vst3Box.state == .on { formats.insert(.vst3) }
        if auBox.state == .on { formats.insert(.au) }
        guard !formats.isEmpty else { status.stringValue = "Tick VST3, Audio Unit or both."; return }
        busy = true
        vst3Box.isEnabled = false
        auBox.isEnabled = false
        action.isEnabled = false
        status.stringValue = "Checking the plug-in and preparing your update…"
        progress.startAnimation(nil)
        let resources = Bundle.main.resourceURL!
        DispatchQueue.global(qos: .userInitiated).async {
            do {
                let manifest = try InstallerEngine.readManifest(resources.appendingPathComponent("payload-manifest.json"))
                let engine = InstallerEngine(payloadRoot: resources.appendingPathComponent("Payload"), manifest: manifest,
                    locations: .standard, runningHosts: { Self.runningHosts(formats: $0) })
                let result = try engine.install(formats: formats)
                let steps = Self.nextSteps(result)
                Self.writeLog(result.log)
                DispatchQueue.main.async {
                    self.progress.stopAnimation(nil)
                    self.busy = false
                    self.status.stringValue = "BACK-SCRATCH \(result.version): \(result.summary).\n\nOpen Live → Settings → Plug-Ins → Rescan. Load BACK-SCRATCH on an audio track, or reopen your existing project."
                    self.action.title = "Done"
                    self.action.action = #selector(self.closeWindow)
                    self.action.isEnabled = true
                    let alert = NSAlert()
                    alert.messageText = "Next steps"
                    alert.informativeText = steps.joined(separator: "\n\n")
                    alert.alertStyle = .informational
                    alert.addButton(withTitle: "OK")
                    alert.beginSheetModal(for: self.window)
                }
            } catch {
                Self.writeLog(["Installation did not complete.", error.localizedDescription])
                DispatchQueue.main.async {
                    self.progress.stopAnimation(nil)
                    self.busy = false
                    self.action.isEnabled = true
                    self.vst3Box.isEnabled = true
                    self.auBox.isEnabled = true
                    self.status.stringValue = error.localizedDescription
                    let alert = NSAlert()
                    alert.messageText = "BACK-SCRATCH could not be installed"
                    alert.informativeText = error.localizedDescription
                    alert.alertStyle = .warning
                    alert.addButton(withTitle: "OK")
                    alert.beginSheetModal(for: self.window)
                }
            }
        }
    }

    static func writeLog(_ lines: [String]) {
        let folder = FileManager.default.homeDirectoryForCurrentUser.appendingPathComponent("Library/Logs/BACK-SCRATCH")
        // Logging is best-effort and cannot turn a completed installation into a failure.
        do {
            try InstallerEngine.refuseSymlinkAncestors(folder.appendingPathComponent("installer.log"))
            try FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)
            let text = ISO8601DateFormatter().string(from: Date()) + "\n" + lines.joined(separator: "\n") + "\n"
            try text.write(to: folder.appendingPathComponent("installer.log"), atomically: true, encoding: .utf8)
        } catch { }
    }

    @objc private func closeWindow() { if !busy { NSApplication.shared.terminate(nil) } }
    func applicationShouldTerminate(_ sender: NSApplication) -> NSApplication.TerminateReply {
        busy ? .terminateCancel : .terminateNow
    }
    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool { !busy }
    func windowShouldClose(_ sender: NSWindow) -> Bool { !busy }
}
