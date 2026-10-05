import AppKit

final class InstallerDelegate: NSObject, NSApplicationDelegate, NSWindowDelegate {
    private var window: NSWindow!
    private let action = NSButton(title: "Install / Update", target: nil, action: nil)
    private let status = NSTextField(wrappingLabelWithString: "Quit Ableton Live and any other audio app before installing.")
    private let progress = NSProgressIndicator()
    private var busy = false

    func applicationDidFinishLaunching(_ notification: Notification) {
        window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 590, height: 490),
                          styleMask: [.titled, .closable, .miniaturizable], backing: .buffered, defer: false)
        window.title = "THROW-UP Installer"
        window.isReleasedWhenClosed = false
        window.delegate = self
        window.center()
        let content = NSView(frame: window.contentView!.bounds)
        content.wantsLayer = true
        content.layer?.backgroundColor = NSColor(calibratedRed: 0.15, green: 0.06, blue: 0.02, alpha: 1).cgColor
        window.contentView = content
        let logo = label("THROW-UP", size: 45, weight: .black, color: .white)
        logo.font = NSFont(descriptor: logo.font!.fontDescriptor.withSymbolicTraits(.italic), size: 45)
        logo.frame = NSRect(x: 34, y: 403, width: 520, height: 59)
        content.addSubview(logo)
        let version = Bundle.main.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String ?? ""
        let build = Bundle.main.object(forInfoDictionaryKey: "CFBundleVersion") as? String ?? version
        let suffix = build == version ? "" : "  •  UPDATED INSTALLER"
        let subtitle = label("MID-BASS REVERB THROWER  •  VERSION \(version)\(suffix)", size: 12, weight: .semibold,
                             color: NSColor(calibratedRed: 1.00, green: 0.54, blue: 0.20, alpha: 1))
        subtitle.frame = NSRect(x: 36, y: 377, width: 520, height: 22)
        content.addSubview(subtitle)
        let explanation = NSTextField(wrappingLabelWithString:
            "Install THROW-UP in your user VST3 folder, or update the copy already there. Existing projects and saved presets stay in place.")
        explanation.font = .systemFont(ofSize: 16)
        explanation.textColor = NSColor(white: 0.94, alpha: 1)
        explanation.frame = NSRect(x: 36, y: 262, width: 518, height: 103)
        content.addSubview(explanation)
        let location = label("~/Library/Audio/Plug-Ins/VST3/THROW-UP.vst3", size: 12, weight: .medium,
                             color: NSColor(calibratedRed: 0.97, green: 0.67, blue: 0.42, alpha: 1))
        location.frame = NSRect(x: 36, y: 232, width: 518, height: 22)
        content.addSubview(location)
        status.font = .systemFont(ofSize: 14)
        status.textColor = NSColor(calibratedRed: 1.00, green: 0.78, blue: 0.50, alpha: 1)
        status.frame = NSRect(x: 36, y: 112, width: 518, height: 102)
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

    private func label(_ text: String, size: CGFloat, weight: NSFont.Weight, color: NSColor) -> NSTextField {
        let result = NSTextField(labelWithString: text)
        result.font = .systemFont(ofSize: size, weight: weight)
        result.textColor = color
        return result
    }

    static func runningHosts(destination: URL) -> [String] {
        let ownPID = ProcessInfo.processInfo.processIdentifier
        let applications = NSWorkspace.shared.runningApplications
        var excludedPIDs: Set<Int32> = [ownPID]
        for app in applications where app.bundleIdentifier?.lowercased() == InstallerHostDetection.installerIdentifier {
            excludedPIDs.insert(app.processIdentifier)
        }
        var found = applications.filter { app in
            InstallerHostDetection.isAudioApplication(bundleIdentifier: app.bundleIdentifier,
                name: app.localizedName, processID: app.processIdentifier, installerProcessID: ownPID)
        }.map { $0.localizedName ?? $0.bundleIdentifier ?? "Audio application" }
        let executable = destination.appendingPathComponent("Contents/MacOS/THROW-UP")
        if InstallerEngine.exists(executable) {
            // Covers hosts we do not recognize by application name, without changing or terminating them.
            if let output = try? InstallerEngine.run("/usr/sbin/lsof", ["-Fpc", "--", executable.path]) {
                found.append(contentsOf: InstallerHostDetection.fileUsers(output, excluding: excludedPIDs))
            }
        }
        return Array(Set(found))
    }

    @objc private func install() {
        guard !busy else { return }
        busy = true
        action.isEnabled = false
        status.stringValue = "Checking the plug-in and preparing your update…"
        progress.startAnimation(nil)
        let resources = Bundle.main.resourceURL!
        let destination = FileManager.default.homeDirectoryForCurrentUser
            .appendingPathComponent("Library/Audio/Plug-Ins/VST3/THROW-UP.vst3", isDirectory: true)
        DispatchQueue.global(qos: .userInitiated).async {
            do {
                let manifest = try InstallerEngine.readManifest(resources.appendingPathComponent("payload-manifest.json"))
                let engine = InstallerEngine(payload: resources.appendingPathComponent("Payload/THROW-UP.vst3"),
                    manifest: manifest, destination: destination,
                    systemCopy: URL(fileURLWithPath: "/Library/Audio/Plug-Ins/VST3/THROW-UP.vst3"),
                    runningHosts: { Self.runningHosts(destination: destination) })
                let result = try engine.install()
                Self.writeLog(result.log)
                DispatchQueue.main.async {
                    self.progress.stopAnimation(nil)
                    self.busy = false
                    self.status.stringValue = "THROW-UP \(result.version) is \(result.updated ? "updated" : "installed").\n\nRescan plug-ins in your DAW, then load THROW-UP on a mid-bass track. Automate the large THROW knob to time your reverb throws."
                    self.action.title = "Done"
                    self.action.action = #selector(self.closeWindow)
                    self.action.isEnabled = true
                }
            } catch {
                Self.writeLog(["Installation did not complete.", error.localizedDescription])
                DispatchQueue.main.async {
                    self.progress.stopAnimation(nil)
                    self.busy = false
                    self.action.isEnabled = true
                    self.status.stringValue = error.localizedDescription
                    let alert = NSAlert()
                    alert.messageText = "THROW-UP could not be installed"
                    alert.informativeText = error.localizedDescription
                    alert.alertStyle = .warning
                    alert.addButton(withTitle: "OK")
                    alert.beginSheetModal(for: self.window)
                }
            }
        }
    }

    static func writeLog(_ lines: [String]) {
        let folder = FileManager.default.homeDirectoryForCurrentUser.appendingPathComponent("Library/Logs/THROW-UP")
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
