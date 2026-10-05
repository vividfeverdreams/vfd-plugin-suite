import AppKit

final class InstallerDelegate: NSObject, NSApplicationDelegate, NSWindowDelegate {
    private var window: NSWindow!
    private let action = NSButton(title: "Install / Update All Five", target: nil, action: nil)
    private let status = NSTextField(wrappingLabelWithString: "Quit your audio applications, then install the complete suite.")
    private let progress = NSProgressIndicator()
    private var busy = false

    func applicationDidFinishLaunching(_ notification: Notification) {
        prepareWindow(present: true)
    }
    private func prepareWindow(present: Bool) {
        NSApplication.shared.appearance = NSAppearance(named: .darkAqua)
        window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 700, height: 745),
            styleMask: [.titled, .closable, .miniaturizable], backing: .buffered, defer: false)
        window.title = "Vivid Fever Dreams Suite"
        window.titlebarAppearsTransparent = true
        window.backgroundColor = NSColor(calibratedRed: 0.065, green: 0.070, blue: 0.085, alpha: 1)
        window.isReleasedWhenClosed = false
        window.delegate = self
        window.center()
        let content = NSView(frame: window.contentView!.bounds)
        content.wantsLayer = true
        content.layer?.backgroundColor = window.backgroundColor.cgColor
        window.contentView = content
        if let url = Bundle.main.url(forResource: "VfdLogo", withExtension: "png"), let image = NSImage(contentsOf: url) {
            let mark = NSImageView(frame: NSRect(x: 42, y: 617, width: 80, height: 84))
            mark.image = image; mark.imageScaling = .scaleProportionallyUpOrDown
            mark.setAccessibilityLabel("Vivid Fever Dreams")
            content.addSubview(mark)
        }
        addLabel("VIVID FEVER DREAMS", frame: NSRect(x: 144, y: 674, width: 506, height: 22),
                 size: 12, weight: .semibold, color: NSColor(white: 0.72, alpha: 1), to: content)
        addLabel("The complete suite", frame: NSRect(x: 141, y: 625, width: 514, height: 49),
                 size: 34, weight: .bold, color: .white, to: content)
        let version = Bundle.main.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String ?? "1.2.2"
        addLabel("MACOS 12+   /   APPLE SILICON   /   VST3   /   SUITE \(version)",
                 frame: NSRect(x: 44, y: 580, width: 614, height: 21), size: 10, weight: .medium,
                 color: NSColor(white: 0.57, alpha: 1), to: content)
        let colors = [NSColor(calibratedRed: 0.69, green: 0.57, blue: 1.0, alpha: 1),
                      NSColor(calibratedRed: 1.0, green: 0.59, blue: 0.25, alpha: 1),
                      NSColor(calibratedRed: 1.0, green: 0.38, blue: 0.46, alpha: 1),
                      NSColor(calibratedRed: 0.50, green: 0.87, blue: 0.38, alpha: 1),
                      NSColor(calibratedRed: 1.0, green: 0.23, blue: 0.70, alpha: 1)]
        let descriptions = ["Talking bass synth", "Mid-bass reverb", "Scratch automation", "Formant shifter", "Magnetic tape-start FX"]
        for (index, definition) in SuiteCatalog.plugins.enumerated() {
            let y = CGFloat(512 - index * 65)
            let row = NSView(frame: NSRect(x: 42, y: y, width: 616, height: 56))
            row.wantsLayer = true
            row.layer?.backgroundColor = NSColor(white: 0.115, alpha: 1).cgColor
            row.layer?.cornerRadius = 10
            let stripe = NSView(frame: NSRect(x: 0, y: 13, width: 3, height: 30))
            stripe.wantsLayer = true; stripe.layer?.backgroundColor = colors[index].cgColor
            stripe.layer?.cornerRadius = 1.5; row.addSubview(stripe)
            let iconURL = Bundle.main.resourceURL!.appendingPathComponent("Payload/\(definition.filename)/Contents/Resources/AppIcon.icns")
            if let image = NSImage(contentsOf: iconURL) {let icon = NSImageView(frame: NSRect(x: 15, y: 4, width: 46, height: 48));icon.image=image;icon.imageScaling = .scaleProportionallyUpOrDown;icon.setAccessibilityLabel(definition.name);row.addSubview(icon)}
            addLabel(definition.name, frame: NSRect(x: 76, y: 25, width: 350, height: 23),
                     size: 17, weight: .bold, color: .white, to: row)
            addLabel(descriptions[index], frame: NSRect(x: 77, y: 7, width: 370, height: 17),
                     size: 11, weight: .regular, color: NSColor(white: 0.65, alpha: 1), to: row)
            let versionLabel = addLabel(definition.version, frame: NSRect(x: 510, y: 18, width: 84, height: 22),
                     size: 13, weight: .medium, color: colors[index], to: row)
            versionLabel.alignment = .right
            content.addSubview(row)
        }
        addLabel("Installs or updates all five in your user VST3 folder.",
                 frame: NSRect(x: 44, y: 218, width: 612, height: 23), size: 13, weight: .regular,
                 color: NSColor(white: 0.85, alpha: 1), to: content)
        addLabel("~/Library/Audio/Plug-Ins/VST3", frame: NSRect(x: 44, y: 193, width: 612, height: 22),
                 size: 12, weight: .medium, color: NSColor(white: 0.58, alpha: 1), to: content)
        status.frame = NSRect(x: 44, y: 104, width: 612, height: 72)
        status.font = .systemFont(ofSize: 13)
        status.textColor = NSColor(white: 0.77, alpha: 1)
        content.addSubview(status)
        action.target = self; action.action = #selector(install)
        action.bezelStyle = .rounded; action.controlSize = .large
        action.font = .systemFont(ofSize: 15, weight: .semibold)
        action.keyEquivalent = "\r"
        action.frame = NSRect(x: 414, y: 39, width: 248, height: 42)
        content.addSubview(action)
        let close = NSButton(title: "Close", target: self, action: #selector(closeWindow))
        close.bezelStyle = .rounded; close.frame = NSRect(x: 38, y: 41, width: 83, height: 36)
        content.addSubview(close)
        let showLog = NSButton(title: "View Log", target: self, action: #selector(viewLog))
        showLog.bezelStyle = .rounded; showLog.frame = NSRect(x: 128, y: 41, width: 96, height: 36)
        content.addSubview(showLog)
        progress.style = .spinning; progress.isDisplayedWhenStopped = false
        progress.frame = NSRect(x: 380, y: 51, width: 20, height: 20)
        content.addSubview(progress)
        if present {
            window.makeKeyAndOrderFront(nil)
            NSApplication.shared.activate(ignoringOtherApps: true)
        }
    }

    // Draw the initial native window without showing it or invoking installation.
    func renderPreview(to url: URL) throws {
        prepareWindow(present: false)
        guard let content = window.contentView,
              let representation = content.bitmapImageRepForCachingDisplay(in: content.bounds) else {
            throw InstallError.message("Could not render the installer preview.")
        }
        content.layoutSubtreeIfNeeded()
        content.cacheDisplay(in: content.bounds, to: representation)
        guard let png = representation.representation(using: .png, properties: [:]) else {
            throw InstallError.message("Could not encode the installer preview.")
        }
        try png.write(to: url)
    }

    @discardableResult private func addLabel(_ text: String, frame: NSRect, size: CGFloat,
        weight: NSFont.Weight, color: NSColor, to view: NSView) -> NSTextField {
        let label = NSTextField(labelWithString: text)
        label.font = .systemFont(ofSize: size, weight: weight); label.textColor = color
        label.frame = frame; view.addSubview(label); return label
    }
    static var userVST3: URL {
        FileManager.default.homeDirectoryForCurrentUser.appendingPathComponent("Library/Audio/Plug-Ins/VST3")
    }
    static func runningHosts(destinationRoot: URL) -> [String] {
        let ownPID = ProcessInfo.processInfo.processIdentifier
        let applications = NSWorkspace.shared.runningApplications
        var excludedPIDs: Set<Int32> = [ownPID]
        for app in applications where InstallerHostDetection.isInstaller(app.bundleIdentifier) { excludedPIDs.insert(app.processIdentifier) }
        var found = applications.filter {
            InstallerHostDetection.isAudioApplication(bundleIdentifier: $0.bundleIdentifier, name: $0.localizedName,
                processID: $0.processIdentifier, installerProcessID: ownPID)
        }.map { $0.localizedName ?? $0.bundleIdentifier ?? "Audio application" }
        let executables = SuiteCatalog.plugins.map {
            destinationRoot.appendingPathComponent("\($0.filename)/Contents/MacOS/\($0.name)")
        }.filter { SuiteInstallerEngine.exists($0) }
        if !executables.isEmpty, let output = try? SuiteInstallerEngine.run("/usr/sbin/lsof", ["-Fpc", "--"] + executables.map(\.path)) {
            found.append(contentsOf: InstallerHostDetection.fileUsers(output, excluding: excludedPIDs))
        }
        return Array(Set(found))
    }
    @objc private func install() {
        guard !busy else { return }
        busy = true; action.isEnabled = false
        status.stringValue = "Checking all five plug-ins and preparing the suite…"
        progress.startAnimation(nil)
        let resources = Bundle.main.resourceURL!
        DispatchQueue.global(qos: .userInitiated).async {
            var engine: SuiteInstallerEngine?
            do {
                let manifest = try SuiteInstallerEngine.readManifest(resources.appendingPathComponent("suite-manifest.json"))
                let installer = SuiteInstallerEngine(payloadRoot: resources.appendingPathComponent("Payload"), manifest: manifest,
                    destinationRoot: Self.userVST3, systemRoot: URL(fileURLWithPath: "/Library/Audio/Plug-Ins/VST3"),
                    runningHosts: { Self.runningHosts(destinationRoot: Self.userVST3) })
                engine = installer
                installer.progress = { message in DispatchQueue.main.async { self.status.stringValue = message } }
                let result = try installer.install()
                Self.writeLog(result.log)
                DispatchQueue.main.async {
                    self.progress.stopAnimation(nil); self.busy = false
                    self.status.stringValue = "Your complete suite is ready. \(result.installed) installed, \(result.updated) updated.\n\nOpen your audio app and rescan its VST3 plug-ins."
                    self.action.title = "Done"; self.action.action = #selector(self.closeWindow); self.action.isEnabled = true
                }
            } catch {
                Self.writeLog((engine?.log ?? []) + ["Installation did not complete.", error.localizedDescription])
                DispatchQueue.main.async {
                    self.progress.stopAnimation(nil); self.busy = false; self.action.isEnabled = true
                    self.status.stringValue = "Installation did not complete. Your log contains the details."
                    let alert = NSAlert()
                    alert.messageText = "The suite could not be installed"
                    alert.informativeText = error.localizedDescription
                    alert.alertStyle = .warning; alert.addButton(withTitle: "OK")
                    alert.beginSheetModal(for: self.window)
                }
            }
        }
    }
    static var logURL: URL {
        FileManager.default.homeDirectoryForCurrentUser.appendingPathComponent("Library/Logs/Vivid Fever Dreams Suite/installer.log")
    }
    static func writeLog(_ lines: [String]) {
        do {
            try SuiteInstallerEngine.refuseSymlinkAncestors(logURL)
            try FileManager.default.createDirectory(at: logURL.deletingLastPathComponent(), withIntermediateDirectories: true)
            let text = ISO8601DateFormatter().string(from: Date()) + "\n" + lines.joined(separator: "\n") + "\n"
            try text.write(to: logURL, atomically: true, encoding: .utf8)
        } catch { /* A logging error cannot invalidate a verified install. */ }
    }
    @objc private func viewLog() {
        if SuiteInstallerEngine.exists(Self.logURL) { NSWorkspace.shared.open(Self.logURL) }
        else { status.stringValue = "A log is saved after the first installation attempt." }
    }
    @objc private func closeWindow() { if !busy { NSApplication.shared.terminate(nil) } }
    func applicationShouldTerminate(_ sender: NSApplication) -> NSApplication.TerminateReply { busy ? .terminateCancel : .terminateNow }
    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool { !busy }
    func windowShouldClose(_ sender: NSWindow) -> Bool { !busy }
}
