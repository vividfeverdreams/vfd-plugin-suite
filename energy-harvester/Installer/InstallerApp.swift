import AppKit

final class InstallerDelegate: NSObject, NSApplicationDelegate, NSWindowDelegate {
    private var window: NSWindow!
    private let action = NSButton(title: "Install / Update", target: nil, action: nil)
    private let status = NSTextField(wrappingLabelWithString: "Quit Ableton Live and any other audio app before installing.")
    private let progress = NSProgressIndicator()
    private var busy = false

    func applicationDidFinishLaunching(_ notification: Notification) {
        installMainMenu()
        window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 590, height: 490),
                          styleMask: [.titled, .closable, .miniaturizable], backing: .buffered, defer: false)
        window.title = "ENERGY HARVESTER Installer"
        window.isReleasedWhenClosed = false
        window.delegate = self
        window.center()
        let content = NSView(frame: window.contentView!.bounds)
        content.wantsLayer = true
        content.layer?.backgroundColor = NSColor(calibratedRed: 0.16, green: 0.04, blue: 0.13, alpha: 1).cgColor
        window.contentView = content
        let logo = label("ENERGY HARVESTER", size: 38, weight: .black, color: .white)
        logo.font = NSFont(descriptor: logo.font!.fontDescriptor.withSymbolicTraits(.italic), size: 38)
        logo.frame = NSRect(x: 34, y: 403, width: 520, height: 59)
        content.addSubview(logo)
        let version = Bundle.main.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String ?? ""
        let build = Bundle.main.object(forInfoDictionaryKey: "CFBundleVersion") as? String ?? version
        let suffix = build == version ? "" : "  •  UPDATED INSTALLER"
        let subtitle = label("MAGNETIC TAPE START  •  VERSION \(version)\(suffix)", size: 12, weight: .semibold,
                             color: NSColor(calibratedRed: 1.00, green: 0.40, blue: 0.77, alpha: 1))
        subtitle.frame = NSRect(x: 36, y: 377, width: 520, height: 22)
        content.addSubview(subtitle)
        let explanation = NSTextField(wrappingLabelWithString:
            "Install ENERGY HARVESTER in your user VST3 folder. One button launches a tape spin-up, with selectable panning, chorus, phaser, flanger and washout, plus tempo sync. Includes the looping circuit-light interface.")
        explanation.font = .systemFont(ofSize: 16)
        explanation.textColor = NSColor(white: 0.94, alpha: 1)
        explanation.frame = NSRect(x: 36, y: 262, width: 518, height: 103)
        content.addSubview(explanation)
        let location = label("~/Library/Audio/Plug-Ins/VST3/ENERGY HARVESTER.vst3", size: 12, weight: .medium,
                             color: NSColor(calibratedRed: 0.97, green: 0.67, blue: 0.84, alpha: 1))
        location.frame = NSRect(x: 36, y: 232, width: 518, height: 22)
        content.addSubview(location)
        status.font = .systemFont(ofSize: 14)
        status.textColor = NSColor(calibratedRed: 1.00, green: 0.78, blue: 0.90, alpha: 1)
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

    private func installMainMenu() {
        let mainMenu = NSMenu()
        let appItem = NSMenuItem()
        let appMenu = NSMenu()
        appMenu.addItem(withTitle: "Quit Install ENERGY HARVESTER", action: #selector(NSApplication.terminate(_:)), keyEquivalent: "q")
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

    static func runningHosts(destination: URL) -> [String] {
        let ownPID = ProcessInfo.processInfo.processIdentifier
        let applications = NSWorkspace.shared.runningApplications
        var excludedPIDs: Set<Int32> = [ownPID]
        for app in applications where InstallerHostDetection.isInstaller(app.bundleIdentifier) {
            excludedPIDs.insert(app.processIdentifier)
        }
        var found = applications.filter { app in
            InstallerHostDetection.isAudioApplication(bundleIdentifier: app.bundleIdentifier,
                name: app.localizedName, processID: app.processIdentifier, installerProcessID: ownPID)
        }.map { $0.localizedName ?? $0.bundleIdentifier ?? "Audio application" }
        let executable = destination.appendingPathComponent("Contents/MacOS/ENERGY HARVESTER")
        if InstallerEngine.exists(executable) {
            // Covers hosts we do not recognize by application name, without changing or terminating them.
            found.append(contentsOf: InstallerHostDetection.openFileUsers(paths: [executable.path], excluding: excludedPIDs))
        }
        return Array(Set(found))
    }

    /// A previous copy that cannot be restored automatically is kept here, outside every folder audio apps scan.
    static var recoveryRoot: URL {
        FileManager.default.homeDirectoryForCurrentUser
            .appendingPathComponent("Library/Application Support/Vivid Fever Dreams/Recovered", isDirectory: true)
    }

    @objc private func install() {
        guard !busy else { return }
        busy = true
        action.isEnabled = false
        status.stringValue = "Checking the plug-in and preparing your update…"
        progress.startAnimation(nil)
        let resources = Bundle.main.resourceURL!
        let destination = FileManager.default.homeDirectoryForCurrentUser
            .appendingPathComponent("Library/Audio/Plug-Ins/VST3/ENERGY HARVESTER.vst3", isDirectory: true)
        DispatchQueue.global(qos: .userInitiated).async {
            do {
                let manifest = try InstallerEngine.readManifest(resources.appendingPathComponent("payload-manifest.json"))
                let engine = InstallerEngine(payload: resources.appendingPathComponent("Payload/ENERGY HARVESTER.vst3"),
                    manifest: manifest, destination: destination,
                    systemCopy: URL(fileURLWithPath: "/Library/Audio/Plug-Ins/VST3/ENERGY HARVESTER.vst3"),
                    recoveryRoot: Self.recoveryRoot,
                    runningHosts: { Self.runningHosts(destination: destination) })
                let result = try engine.install()
                Self.writeLog(result.log)
                DispatchQueue.main.async {
                    self.progress.stopAnimation(nil)
                    self.busy = false
                    self.status.stringValue = "ENERGY HARVESTER \(result.version) is \(result.updated ? "updated" : "installed").\n\nRescan plug-ins in your DAW, then load ENERGY HARVESTER on a track. Automate HARVEST from OFF to ON to launch a spin-up. Switch OFF to rearm."
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
                    alert.messageText = "ENERGY HARVESTER could not be installed"
                    alert.informativeText = error.localizedDescription
                    alert.alertStyle = .warning
                    alert.addButton(withTitle: "OK")
                    alert.beginSheetModal(for: self.window)
                }
            }
        }
    }

    static func writeLog(_ lines: [String]) {
        let folder = FileManager.default.homeDirectoryForCurrentUser.appendingPathComponent("Library/Logs/ENERGY HARVESTER")
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
