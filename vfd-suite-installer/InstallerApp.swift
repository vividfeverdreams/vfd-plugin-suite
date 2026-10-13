import AppKit

private final class FlippedView: NSView { override var isFlipped: Bool { true } }

/// Text for the result page, kept separate from the views so it can be previewed and reasoned about on its own.
enum ResultSummary {
    struct Section { let heading: String; let lines: [String] }
    static func installSections(_ result: InstallResult, hostIdentifiers: Set<String>, whatsNew: [String: [String]]) -> [Section] {
        var sections: [Section] = []
        var done: [String] = []
        if !result.installed.isEmpty { done.append("Installed: " + result.installed.map(\.title).joined(separator: ", ")) }
        if !result.updated.isEmpty { done.append("Updated: " + result.updated.map(\.title).joined(separator: ", ")) }
        if !result.skipped.isEmpty { done.append("Already up to date, left untouched: " + result.skipped.map(\.title).joined(separator: ", ")) }
        sections.append(Section(heading: "Done", lines: done))
        let changed = result.installed + result.updated
        var steps = RescanGuidance.steps(installedBundleIdentifiers: hostIdentifiers,
                                         formats: Set((changed.isEmpty ? result.skipped : changed).map(\.format)))
        if result.audioUnitsRefreshed {
            steps.append("macOS was asked to refresh its Audio Unit list. Restart Logic Pro, GarageBand or MainStage before looking for the new Audio Units.")
        }
        if !steps.isEmpty { sections.append(Section(heading: "Next steps", lines: steps)) }
        var seen = Set<Int>()
        var notes: [String] = []
        for key in changed.sorted() where seen.insert(key.plugin).inserted {
            let definition = key.definition
            if let lines = whatsNew[definition.name] { notes.append("\(definition.name) \(definition.version): " + lines.joined(separator: " · ")) }
        }
        if !notes.isEmpty { sections.append(Section(heading: "What’s new", lines: notes)) }
        for warning in result.warnings { sections.append(Section(heading: "Needs attention", lines: [warning])) }
        return sections
    }
    static func uninstallSections(_ result: UninstallResult) -> [Section] {
        var lines = result.removed.map { "\($0.key.title) is in the Trash. Drag it back to its folder to restore it." }
        if !result.notInstalled.isEmpty { lines.append("Not installed, nothing to remove: " + result.notInstalled.map(\.title).joined(separator: ", ")) }
        var sections = [Section(heading: "Removed", lines: lines)]
        var next = ["Rescan plug-ins in your audio app so it forgets the removed plug-ins."]
        if result.audioUnitsRefreshed { next.append("macOS was asked to refresh its Audio Unit list. Restart Logic Pro, GarageBand or MainStage.") }
        sections.append(Section(heading: "Next steps", lines: next))
        return sections
    }
    static func attributed(_ sections: [Section]) -> NSAttributedString {
        let text = NSMutableAttributedString()
        let heading: [NSAttributedString.Key: Any] = [.font: NSFont.systemFont(ofSize: 14, weight: .semibold), .foregroundColor: NSColor.white]
        let paragraph = NSMutableParagraphStyle()
        paragraph.headIndent = 14
        paragraph.paragraphSpacing = 5
        let body: [NSAttributedString.Key: Any] = [.font: NSFont.systemFont(ofSize: 12.5), .foregroundColor: NSColor(white: 0.82, alpha: 1),
                                                   .paragraphStyle: paragraph]
        for (index, section) in sections.enumerated() {
            text.append(NSAttributedString(string: (index == 0 ? "" : "\n") + section.heading + "\n", attributes: heading))
            for line in section.lines { text.append(NSAttributedString(string: "•  " + line + "\n", attributes: body)) }
        }
        return text
    }
}

final class InstallerDelegate: NSObject, NSApplicationDelegate, NSWindowDelegate {
    /// One 66-point row per plug-in; the heading moves up with them. With six plug-ins the window fits a 1280×800
    /// display with the menu bar and a small Dock visible; the content scrolls on anything shorter.
    private static let rowsHeight = CGFloat(SuiteCatalog.plugins.count * 66)
    private static let contentSize = NSSize(width: 700, height: 310 + rowsHeight)
    private static let descriptions = ["Talking bass synth", "Mid-bass reverb", "Scratch automation", "Formant shifter", "Magnetic tape-start FX",
                                       "Swing gate + slap delay"]
    private static let colors = [NSColor(calibratedRed: 0.69, green: 0.57, blue: 1.0, alpha: 1),
                                 NSColor(calibratedRed: 1.0, green: 0.59, blue: 0.25, alpha: 1),
                                 NSColor(calibratedRed: 1.0, green: 0.38, blue: 0.46, alpha: 1),
                                 NSColor(calibratedRed: 0.50, green: 0.87, blue: 0.38, alpha: 1),
                                 NSColor(calibratedRed: 1.0, green: 0.23, blue: 0.70, alpha: 1),
                                 NSColor(calibratedRed: 0.37, green: 0.91, blue: 0.85, alpha: 1)]
    private enum Page { case main, result, repair }
    private var window: NSWindow!
    private var documentView: NSView!
    private let rows = NSView(frame: NSRect(x: 42, y: 180, width: 616, height: InstallerDelegate.rowsHeight))
    private var checkboxes: [ItemKey: NSButton] = [:]
    private var statusLabels: [ItemKey: NSTextField] = [:]
    private let folders = NSTextField(wrappingLabelWithString:
        "VST3 installs to ~/Library/Audio/Plug-Ins/VST3 and AU to ~/Library/Audio/Plug-Ins/Components. Items already up to date are left untouched.")
    private let status = NSTextField(wrappingLabelWithString: "Checking what is installed…")
    private let progress = NSProgressIndicator()
    private let closeButton = NSButton(title: "Close", target: nil, action: nil)
    private let logButton = NSButton(title: "View Log", target: nil, action: nil)
    private let repairButton = NSButton(title: "Repair…", target: nil, action: nil)
    private let uninstallButton = NSButton(title: "Uninstall…", target: nil, action: nil)
    private let backButton = NSButton(title: "Back", target: nil, action: nil)
    private let action = NSButton(title: "Install / Update Selected", target: nil, action: nil)
    private let confirmPanel = NSView(frame: NSRect(x: 38, y: 10, width: 624, height: 50))
    private let confirmText = NSTextField(wrappingLabelWithString: "")
    private let confirmCancel = NSButton(title: "Cancel", target: nil, action: nil)
    private let confirmOK = NSButton(title: "Move to Trash", target: nil, action: nil)
    private var confirmAction: (() -> Void)?
    /// The result and repair pages also use the space of the folders note, which only the main page shows.
    private let detailScroll = NSScrollView(frame: NSRect(x: 42, y: 140, width: 616, height: InstallerDelegate.rowsHeight + 40))
    private let detailText = NSTextView(frame: NSRect(x: 0, y: 0, width: 600, height: InstallerDelegate.rowsHeight))
    private let repairList = FlippedView(frame: NSRect(x: 0, y: 0, width: 600, height: InstallerDelegate.rowsHeight))
    private var page = Page.main
    private var busy = false
    private var engine: SuiteInstallerEngine?
    private var states: [ItemKey: ItemState] = [:]
    private var issues: [RepairIssue] = []
    private var whatsNew: [String: [String]] = [:]
    /// Set while drawing --render-preview sample states, which have no engine.
    private var previewing = false

    func applicationDidFinishLaunching(_ notification: Notification) {
        installMainMenu()
        prepareWindow(present: true)
        refresh(firstLoad: true)
    }
    private func installMainMenu() {
        let mainMenu = NSMenu()
        let appItem = NSMenuItem()
        let appMenu = NSMenu()
        appMenu.addItem(withTitle: "Quit Install Vivid Fever Dreams Suite", action: #selector(NSApplication.terminate(_:)), keyEquivalent: "q")
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

    // MARK: Layout

    private func prepareWindow(present: Bool) {
        NSApplication.shared.appearance = NSAppearance(named: .darkAqua)
        let background = NSColor(calibratedRed: 0.065, green: 0.070, blue: 0.085, alpha: 1)
        let visibleHeight = (NSScreen.main?.visibleFrame.height ?? 900) - 40
        let height = present ? min(Self.contentSize.height, max(360, visibleHeight)) : Self.contentSize.height
        window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: Self.contentSize.width, height: height),
            styleMask: [.titled, .closable, .miniaturizable], backing: .buffered, defer: false)
        window.title = "Vivid Fever Dreams Suite"
        window.titlebarAppearsTransparent = true
        window.backgroundColor = background
        window.isReleasedWhenClosed = false
        window.delegate = self
        window.center()
        let content = NSView(frame: NSRect(origin: .zero, size: Self.contentSize))
        content.wantsLayer = true
        content.layer?.backgroundColor = background.cgColor
        documentView = content
        let scroll = NSScrollView(frame: NSRect(x: 0, y: 0, width: Self.contentSize.width, height: height))
        scroll.drawsBackground = true
        scroll.backgroundColor = background
        scroll.hasVerticalScroller = true
        scroll.autohidesScrollers = true
        scroll.documentView = content
        window.contentView = scroll
        if let url = Bundle.main.url(forResource: "VfdLogo", withExtension: "png"), let image = NSImage(contentsOf: url) {
            let mark = NSImageView(frame: NSRect(x: 42, y: 218 + Self.rowsHeight, width: 64, height: 67))
            mark.image = image; mark.imageScaling = .scaleProportionallyUpOrDown
            mark.setAccessibilityLabel("Vivid Fever Dreams")
            content.addSubview(mark)
        }
        addLabel("VIVID FEVER DREAMS", frame: NSRect(x: 122, y: 266 + Self.rowsHeight, width: 520, height: 20),
                 size: 12, weight: .semibold, color: NSColor(white: 0.72, alpha: 1), to: content)
        addLabel("The complete suite", frame: NSRect(x: 120, y: 222 + Self.rowsHeight, width: 520, height: 44),
                 size: 30, weight: .bold, color: .white, to: content)
        let version = Bundle.main.object(forInfoDictionaryKey: "CFBundleShortVersionString") as? String ?? "1.6.0"
        addLabel("MACOS 12+   /   APPLE SILICON   /   VST3 + AUDIO UNIT   /   SUITE \(version)",
                 frame: NSRect(x: 44, y: 190 + Self.rowsHeight, width: 614, height: 18), size: 10, weight: .medium,
                 color: NSColor(white: 0.57, alpha: 1), to: content)
        buildRows()
        content.addSubview(rows)
        detailText.isEditable = false
        detailText.isSelectable = true
        detailText.drawsBackground = false
        detailText.textContainerInset = NSSize(width: 12, height: 12)
        detailText.isVerticallyResizable = true
        detailText.autoresizingMask = [.width]
        detailScroll.drawsBackground = true
        detailScroll.backgroundColor = NSColor(white: 0.10, alpha: 1)
        detailScroll.hasVerticalScroller = true
        detailScroll.autohidesScrollers = true
        detailScroll.wantsLayer = true
        detailScroll.layer?.cornerRadius = 10
        detailScroll.isHidden = true
        content.addSubview(detailScroll)
        folders.frame = NSRect(x: 44, y: 140, width: 612, height: 34)
        folders.font = .systemFont(ofSize: 11)
        folders.textColor = NSColor(white: 0.60, alpha: 1)
        content.addSubview(folders)
        status.frame = NSRect(x: 44, y: 66, width: 612, height: 70)
        status.font = .systemFont(ofSize: 13)
        status.textColor = NSColor(white: 0.80, alpha: 1)
        status.isSelectable = true
        status.cell?.truncatesLastVisibleLine = true
        content.addSubview(status)
        configure(closeButton, #selector(closeWindow), NSRect(x: 38, y: 18, width: 78, height: 34), in: content)
        configure(logButton, #selector(viewLog), NSRect(x: 120, y: 18, width: 90, height: 34), in: content)
        configure(repairButton, #selector(showRepair), NSRect(x: 214, y: 18, width: 86, height: 34), in: content)
        configure(uninstallButton, #selector(confirmUninstall), NSRect(x: 330, y: 18, width: 104, height: 34), in: content)
        configure(backButton, #selector(goBack), NSRect(x: 330, y: 18, width: 104, height: 34), in: content)
        configure(action, #selector(primaryAction), NSRect(x: 440, y: 14, width: 222, height: 42), in: content)
        action.controlSize = .large
        action.font = .systemFont(ofSize: 15, weight: .semibold)
        action.keyEquivalent = "\r"
        progress.style = .spinning; progress.isDisplayedWhenStopped = false
        progress.frame = NSRect(x: 306, y: 26, width: 18, height: 18)
        progress.setAccessibilityLabel("Working")
        content.addSubview(progress)
        confirmPanel.wantsLayer = true
        confirmPanel.layer?.backgroundColor = NSColor(calibratedRed: 0.20, green: 0.10, blue: 0.12, alpha: 1).cgColor
        confirmPanel.layer?.cornerRadius = 10
        confirmText.frame = NSRect(x: 12, y: 6, width: 380, height: 38)
        confirmText.font = .systemFont(ofSize: 12)
        confirmText.textColor = .white
        confirmPanel.addSubview(confirmText)
        configure(confirmCancel, #selector(cancelConfirm), NSRect(x: 400, y: 8, width: 96, height: 34), in: confirmPanel)
        configure(confirmOK, #selector(acceptConfirm), NSRect(x: 500, y: 8, width: 118, height: 34), in: confirmPanel)
        if #available(macOS 11.0, *) { confirmOK.hasDestructiveAction = true }
        confirmPanel.isHidden = true
        content.addSubview(confirmPanel)
        show(.main)
        // Start at the top of the content when the window is shorter than the layout.
        scroll.contentView.scroll(to: NSPoint(x: 0, y: max(0, Self.contentSize.height - height)))
        scroll.reflectScrolledClipView(scroll.contentView)
        if present {
            window.makeKeyAndOrderFront(nil)
            NSApplication.shared.activate(ignoringOtherApps: true)
        }
    }
    private func buildRows() {
        for (index, definition) in SuiteCatalog.plugins.enumerated() {
            let row = NSView(frame: NSRect(x: 0, y: CGFloat((SuiteCatalog.plugins.count - 1 - index) * 66), width: 616, height: 58))
            row.wantsLayer = true
            row.layer?.backgroundColor = NSColor(white: 0.115, alpha: 1).cgColor
            row.layer?.cornerRadius = 10
            let stripe = NSView(frame: NSRect(x: 0, y: 14, width: 3, height: 30))
            stripe.wantsLayer = true; stripe.layer?.backgroundColor = Self.colors[index].cgColor
            stripe.layer?.cornerRadius = 1.5; row.addSubview(stripe)
            if let resources = Bundle.main.resourceURL,
               let image = NSImage(contentsOf: resources.appendingPathComponent("Payload/\(definition.filename(for: .vst3))/Contents/Resources/AppIcon.icns")) {
                let icon = NSImageView(frame: NSRect(x: 14, y: 7, width: 44, height: 44))
                icon.image = image; icon.imageScaling = .scaleProportionallyUpOrDown
                icon.setAccessibilityLabel(definition.name)
                row.addSubview(icon)
            }
            addLabel(definition.name, frame: NSRect(x: 70, y: 31, width: 186, height: 22), size: 16, weight: .bold, color: .white, to: row)
            addLabel(Self.descriptions[index], frame: NSRect(x: 71, y: 12, width: 186, height: 17), size: 11, weight: .regular,
                     color: NSColor(white: 0.65, alpha: 1), to: row)
            for (column, format) in PluginFormat.allCases.sorted().enumerated() {
                let key = ItemKey(plugin: index, format: format)
                let x = CGFloat(262 + column * 178)
                let checkbox = NSButton(checkboxWithTitle: "\(format.label)  \(definition.version)", target: self, action: #selector(selectionChanged))
                checkbox.frame = NSRect(x: x, y: 31, width: 170, height: 20)
                checkbox.font = .systemFont(ofSize: 12, weight: .semibold)
                checkbox.state = .on
                checkbox.setAccessibilityLabel("\(definition.name) \(format.label)")
                row.addSubview(checkbox)
                checkboxes[key] = checkbox
                let label = addLabel("Checking…", frame: NSRect(x: x + 18, y: 10, width: 158, height: 17), size: 10.5, weight: .medium,
                                     color: NSColor(white: 0.62, alpha: 1), to: row)
                statusLabels[key] = label
            }
            rows.addSubview(row)
        }
    }
    private func configure(_ button: NSButton, _ selector: Selector, _ frame: NSRect, in view: NSView) {
        button.target = self; button.action = selector
        button.bezelStyle = .rounded; button.frame = frame
        view.addSubview(button)
    }
    @discardableResult private func addLabel(_ text: String, frame: NSRect, size: CGFloat,
        weight: NSFont.Weight, color: NSColor, to view: NSView) -> NSTextField {
        let label = NSTextField(labelWithString: text)
        label.font = .systemFont(ofSize: size, weight: weight); label.textColor = color
        label.frame = frame; view.addSubview(label); return label
    }
    private func show(_ newPage: Page) {
        page = newPage
        rows.isHidden = newPage != .main
        folders.isHidden = newPage != .main
        detailScroll.isHidden = newPage == .main
        repairButton.isHidden = newPage != .main
        uninstallButton.isHidden = newPage != .main
        backButton.isHidden = newPage == .main
        action.isHidden = newPage == .repair
        action.title = newPage == .result ? "Done" : "Install / Update Selected"
        hideConfirm()
    }

    // MARK: Status

    /// Updates the status text and announces it to VoiceOver.
    private func setStatus(_ text: String) {
        status.stringValue = text
        NSAccessibility.post(element: window as Any, notification: .announcementRequested,
                             userInfo: [.announcement: text, .priority: NSAccessibilityPriorityLevel.high.rawValue])
    }
    private func setBusy(_ value: Bool) {
        busy = value
        if value { progress.startAnimation(nil) } else { progress.stopAnimation(nil) }
        for button in [action, repairButton, uninstallButton, backButton, confirmOK, confirmCancel] { button.isEnabled = !value }
        for box in checkboxes.values { box.isEnabled = !value }
    }
    private func apply(states newStates: [ItemKey: ItemState], resetSelection: Bool) {
        states = newStates
        for key in SuiteCatalog.allItems {
            guard let state = newStates[key], let label = statusLabels[key], let box = checkboxes[key] else { continue }
            label.stringValue = state.label(newVersion: key.definition.version)
            switch state {
            case .newer, .blocked: label.textColor = NSColor(calibratedRed: 1.0, green: 0.62, blue: 0.35, alpha: 1)
            case .upToDate: label.textColor = NSColor(calibratedRed: 0.55, green: 0.85, blue: 0.55, alpha: 1)
            default: label.textColor = NSColor(white: 0.70, alpha: 1)
            }
            if case .blocked(let reason) = state { box.toolTip = reason; label.toolTip = reason } else { box.toolTip = nil; label.toolTip = nil }
            if resetSelection { box.state = state.blocksInstall ? .off : .on }
        }
        updateActionTitle()
    }
    private var selection: Set<ItemKey> { Set(checkboxes.filter { $0.value.state == .on }.map(\.key)) }
    @objc private func selectionChanged() { updateActionTitle() }
    private func updateActionTitle() {
        guard page == .main else { return }
        let selected = selection
        action.isEnabled = !busy && !selected.isEmpty && (engine != nil || previewing)
        let pending = selected.filter { states[$0] != .upToDate }
        action.title = selected.isEmpty ? "Select a format" : (pending.isEmpty ? "Everything selected is up to date" : "Install / Update \(pending.count) Selected")
    }

    // MARK: Loading

    static func installedHostIdentifiers() -> Set<String> {
        Set(RescanGuidance.allBundleIdentifiers.filter { NSWorkspace.shared.urlForApplication(withBundleIdentifier: $0) != nil })
    }
    private func makeEngine() throws -> SuiteInstallerEngine {
        let resources = Bundle.main.resourceURL ?? Bundle.main.bundleURL.appendingPathComponent("Contents/Resources")
        let manifest = try SuiteInstallerEngine.readManifest(resources.appendingPathComponent("suite-manifest.json"))
        let engine = SuiteInstallerEngine(payloadRoot: resources.appendingPathComponent("Payload"), manifest: manifest,
            locations: .standard, runningHosts: { Self.runningHosts(formats: $0) })
        try engine.validateManifest()
        if let data = try? Data(contentsOf: resources.appendingPathComponent("whats-new.json")) {
            whatsNew = (try? WhatsNew.parse(data, requiredNames: SuiteCatalog.plugins.map(\.name), allowOtherNames: false)) ?? [:]
        }
        return engine
    }
    private func refresh(firstLoad: Bool) {
        setBusy(true)
        let existing = engine
        DispatchQueue.global(qos: .userInitiated).async {
            do {
                let engine = try existing ?? self.makeEngine()
                let newStates = engine.states()
                let newIssues = engine.repairIssues()
                DispatchQueue.main.async {
                    self.engine = engine
                    self.issues = newIssues
                    self.setBusy(false)
                    self.apply(states: newStates, resetSelection: true)
                    if self.page == .repair { self.renderRepairList() }
                    if !newIssues.isEmpty && self.page == .main {
                        self.setStatus("An interrupted installation or a leftover copy needs attention. Click Repair… to restore your previous plug-ins or clean it up.")
                    } else if firstLoad || self.page == .main {
                        self.setStatus("Quit your audio apps, choose the formats you want, then click Install / Update. Logic Pro and GarageBand use the AU format.")
                    }
                }
            } catch {
                let message = UserFacingText.message(for: error)
                DispatchQueue.main.async {
                    self.setBusy(false)
                    self.action.isEnabled = false
                    self.setStatus("This installer can’t be used: \(message)")
                }
            }
        }
    }

    // MARK: Hosts

    static func runningHosts(formats: Set<PluginFormat>) -> [String] {
        let ownPID = ProcessInfo.processInfo.processIdentifier
        let applications = NSWorkspace.shared.runningApplications
        var excludedPIDs: Set<Int32> = [ownPID]
        for app in applications where InstallerHostDetection.isInstaller(app.bundleIdentifier) { excludedPIDs.insert(app.processIdentifier) }
        var found = applications.filter {
            InstallerHostDetection.isAudioApplication(bundleIdentifier: $0.bundleIdentifier, name: $0.localizedName,
                processID: $0.processIdentifier, installerProcessID: ownPID, formats: formats)
        }.map { $0.localizedName ?? $0.bundleIdentifier ?? "Audio application" }
        // Any process with an installed suite executable mapped is a host, whether or not it is listed above.
        let locations = InstallLocations.standard
        let executables = SuiteCatalog.allItems.filter { formats.contains($0.format) }.map {
            locations.destinationRoot($0.format).appendingPathComponent("\($0.filename)/Contents/MacOS/\($0.definition.name)")
        }.filter { SuiteInstallerEngine.exists($0) }.map(\.path)
        found.append(contentsOf: InstallerHostDetection.openFileUsers(paths: executables, excluding: excludedPIDs))
        return Array(Set(found))
    }

    // MARK: Actions

    @objc private func primaryAction() {
        if page == .result { closeWindow(); return }
        install()
    }
    private func install() {
        guard !busy, let engine = engine else { return }
        let selected = selection
        guard !selected.isEmpty else { setStatus("Tick at least one VST3 or AU box."); return }
        setBusy(true)
        setStatus("Checking the selected plug-ins and preparing the installation…")
        engine.progress = { message in DispatchQueue.main.async { self.setStatus(message) } }
        DispatchQueue.global(qos: .userInitiated).async {
            do {
                let result = try engine.install(selection: selected)
                Self.writeLog(result.log)
                let sections = ResultSummary.installSections(result, hostIdentifiers: Self.installedHostIdentifiers(), whatsNew: self.whatsNew)
                DispatchQueue.main.async {
                    self.setBusy(false)
                    self.showResult(sections)
                    let changed = result.installed.count + result.updated.count
                    self.setStatus(changed == 0 ? "Everything selected was already up to date." :
                        "Done: \(result.installed.count) installed, \(result.updated.count) updated, \(result.skipped.count) already up to date.")
                }
            } catch {
                Self.writeLog(engine.log + ["Installation did not complete.", UserFacingText.logText(for: error)])
                self.finishWithError(error, title: "The suite could not be installed")
            }
        }
    }
    @objc private func confirmUninstall() {
        guard !busy else { return }
        let installed = selection.filter { if case .notInstalled = states[$0] ?? .notInstalled { return false }; return true }.sorted()
        guard !installed.isEmpty else { setStatus("None of the selected formats are installed, so there is nothing to remove."); return }
        setStatus("Will move to the Trash: \(installed.map(\.title).joined(separator: ", ")).")
        showConfirm("Move \(installed.count) selected plug-in\(installed.count == 1 ? "" : "s") to the Trash? You can put \(installed.count == 1 ? "it" : "them") back from the Trash.",
                    ok: "Move to Trash") { self.uninstall(Set(installed)) }
    }
    private func uninstall(_ keys: Set<ItemKey>) {
        guard let engine = engine else { return }
        setBusy(true)
        DispatchQueue.global(qos: .userInitiated).async {
            do {
                let result = try engine.uninstall(selection: keys)
                Self.writeLog(result.log)
                DispatchQueue.main.async {
                    self.setBusy(false)
                    self.showResult(ResultSummary.uninstallSections(result))
                    self.setStatus("Moved \(result.removed.count) plug-in bundle\(result.removed.count == 1 ? "" : "s") to the Trash.")
                }
            } catch {
                Self.writeLog(engine.log + ["Uninstall did not complete.", UserFacingText.logText(for: error)])
                self.finishWithError(error, title: "The plug-ins could not be removed")
            }
        }
    }
    @objc private func showRepair() {
        guard !busy else { return }
        show(.repair)
        renderRepairList()
        setStatus(issues.isEmpty ? "Nothing needs repair." : "Choose what to do with each item. Everything you remove goes to the Trash.")
        refresh(firstLoad: false)
    }
    private func repair(_ issue: RepairIssue, restore: Bool) {
        guard let engine = engine else { return }
        setBusy(true)
        DispatchQueue.global(qos: .userInitiated).async {
            do {
                if restore { _ = try engine.restorePrevious(issue) } else { _ = try engine.cleanUp(issue) }
                Self.writeLog(engine.log)
                DispatchQueue.main.async {
                    self.setBusy(false)
                    self.setStatus(restore ? "Restored the previous versions from \(issue.url.lastPathComponent)." :
                        "Moved \(issue.url.lastPathComponent) to the Trash.")
                    self.refresh(firstLoad: false)
                }
            } catch {
                Self.writeLog(engine.log + ["Repair did not complete.", UserFacingText.logText(for: error)])
                self.finishWithError(error, title: "Repair did not complete")
            }
        }
    }
    private func finishWithError(_ error: Error, title: String) {
        let message = UserFacingText.message(for: error)
        DispatchQueue.main.async {
            self.setBusy(false)
            self.updateActionTitle()
            self.setStatus("Did not complete. \(message)")
            let alert = NSAlert()
            alert.messageText = title
            alert.informativeText = message
            alert.alertStyle = .warning; alert.addButton(withTitle: "OK")
            alert.beginSheetModal(for: self.window)
        }
    }
    @objc private func goBack() {
        show(.main)
        refresh(firstLoad: false)
    }

    // MARK: Detail pages

    private func showResult(_ sections: [ResultSummary.Section]) {
        show(.result)
        detailText.textStorage?.setAttributedString(ResultSummary.attributed(sections))
        detailText.frame = NSRect(x: 0, y: 0, width: detailScroll.contentSize.width, height: detailScroll.contentSize.height)
        detailScroll.documentView = detailText
        detailText.sizeToFit()
        detailText.scrollToBeginningOfDocument(nil)
    }
    private func renderRepairList() {
        repairList.subviews.forEach { $0.removeFromSuperview() }
        let width = detailScroll.contentSize.width
        var y: CGFloat = 12
        if issues.isEmpty {
            let label = NSTextField(wrappingLabelWithString: "Nothing needs repair. No interrupted installations, leftover temporary copies or saved previous copies were found.")
            label.frame = NSRect(x: 14, y: y, width: width - 28, height: 40)
            label.font = .systemFont(ofSize: 13); label.textColor = NSColor(white: 0.82, alpha: 1)
            repairList.addSubview(label)
            y += 52
        }
        for (index, issue) in issues.enumerated() {
            let title = NSTextField(labelWithString: issue.title)
            title.frame = NSRect(x: 14, y: y, width: width - 28, height: 18)
            title.font = .systemFont(ofSize: 13, weight: .semibold); title.textColor = .white
            repairList.addSubview(title)
            let detail = NSTextField(wrappingLabelWithString: "\(issue.detail)\n\(issue.url.path)")
            detail.frame = NSRect(x: 14, y: y + 20, width: width - 28, height: 46)
            detail.font = .systemFont(ofSize: 11); detail.textColor = NSColor(white: 0.68, alpha: 1)
            detail.isSelectable = true
            detail.cell?.truncatesLastVisibleLine = true
            repairList.addSubview(detail)
            var x: CGFloat = 12
            func addButton(_ text: String, _ selector: Selector, width buttonWidth: CGFloat) {
                let button = NSButton(title: text, target: self, action: selector)
                button.bezelStyle = .rounded; button.controlSize = .small; button.font = .systemFont(ofSize: 11)
                button.frame = NSRect(x: x, y: y + 68, width: buttonWidth, height: 24)
                button.tag = index
                button.isEnabled = !busy
                repairList.addSubview(button)
                x += buttonWidth + 8
            }
            if issue.canRestore { addButton("Restore Previous Versions", #selector(confirmRestore(_:)), width: 176) }
            addButton("Clean Up…", #selector(confirmCleanUp(_:)), width: 96)
            addButton("Reveal in Finder", #selector(reveal(_:)), width: 120)
            y += 104
        }
        repairList.frame = NSRect(x: 0, y: 0, width: width, height: max(y, detailScroll.contentSize.height))
        detailScroll.documentView = repairList
    }
    @objc private func confirmRestore(_ sender: NSButton) {
        guard issues.indices.contains(sender.tag) else { return }
        let issue = issues[sender.tag]
        showConfirm("Restore the previous versions saved in “\(issue.url.lastPathComponent)”? Plug-ins it had added go to the Trash.", ok: "Restore") {
            self.repair(issue, restore: true)
        }
    }
    @objc private func confirmCleanUp(_ sender: NSButton) {
        guard issues.indices.contains(sender.tag) else { return }
        let issue = issues[sender.tag]
        let warning: String
        if case .suiteTransaction(_, let safe) = issue.kind, !safe {
            warning = " Its backup copies go with it; your installed plug-ins stay as they are."
        } else { warning = "" }
        showConfirm("Move “\(issue.url.lastPathComponent)” to the Trash?\(warning)", ok: "Move to Trash") { self.repair(issue, restore: false) }
    }
    @objc private func reveal(_ sender: NSButton) {
        guard issues.indices.contains(sender.tag) else { return }
        NSWorkspace.shared.activateFileViewerSelecting([issues[sender.tag].url])
        setStatus("Shown in Finder. Hidden items need Command-Shift-Period to appear.")
    }

    // MARK: In-window confirmation

    private func showConfirm(_ text: String, ok: String, action perform: @escaping () -> Void) {
        confirmText.stringValue = text
        confirmOK.title = ok
        confirmAction = perform
        confirmPanel.isHidden = false
        for button in [closeButton, logButton, repairButton, uninstallButton, backButton, action] { button.isHidden = true }
        progress.isHidden = true
        window.makeFirstResponder(confirmCancel)
    }
    private func hideConfirm() {
        confirmPanel.isHidden = true
        confirmAction = nil
        progress.isHidden = false
        closeButton.isHidden = false
        logButton.isHidden = false
        repairButton.isHidden = page != .main
        uninstallButton.isHidden = page != .main
        backButton.isHidden = page == .main
        action.isHidden = page == .repair
    }
    @objc private func cancelConfirm() {
        hideConfirm()
        setStatus("Cancelled. Nothing was changed.")
    }
    @objc private func acceptConfirm() {
        let perform = confirmAction
        hideConfirm()
        perform?()
    }

    // MARK: Preview

    /// Draws one window state with representative sample data, without reading the plug-in folders or changing anything.
    func renderPreview(to url: URL, state previewState: String) throws {
        previewing = true
        prepareWindow(present: false)
        setBusy(false)
        var sample: [ItemKey: ItemState] = [:]
        for key in SuiteCatalog.allItems { sample[key] = key.format == .au ? .notInstalled : .older("0.0.9") }
        sample[ItemKey(plugin: 0, format: .vst3)] = .older("0.1.6")
        sample[ItemKey(plugin: 1, format: .vst3)] = .older("0.1.5")
        sample[ItemKey(plugin: 2, format: .vst3)] = .older("0.10.2")
        sample[ItemKey(plugin: 3, format: .vst3)] = .upToDate
        sample[ItemKey(plugin: 4, format: .vst3)] = .newer("0.4.1")
        apply(states: sample, resetSelection: true)
        if let data = try? Data(contentsOf: (Bundle.main.resourceURL ?? URL(fileURLWithPath: "/")).appendingPathComponent("whats-new.json")) {
            whatsNew = (try? WhatsNew.parse(data, requiredNames: SuiteCatalog.plugins.map(\.name), allowOtherNames: false)) ?? [:]
        }
        if whatsNew.isEmpty {
            whatsNew = Dictionary(uniqueKeysWithValues: SuiteCatalog.plugins.map { ($0.name, ["Now also available as an Audio Unit for Logic Pro and GarageBand."]) })
        }
        switch previewState {
        case "uninstall":
            setStatus("Will move to the Trash: YOI-BOI VST3, THROW-UP VST3, BACK-SCRATCH VST3, SHIFTY VST3.")
            showConfirm("Move 4 selected plug-ins to the Trash? You can put them back from the Trash.", ok: "Move to Trash") {}
        case "result":
            let result = InstallResult(installed: SuiteCatalog.allItems.filter { $0.format == .au },
                                       updated: [ItemKey(plugin: 0, format: .vst3), ItemKey(plugin: 1, format: .vst3), ItemKey(plugin: 2, format: .vst3)],
                                       skipped: [ItemKey(plugin: 3, format: .vst3)], audioUnitsRefreshed: true, log: [], warnings: [])
            showResult(ResultSummary.installSections(result, hostIdentifiers: ["com.ableton.live", "com.apple.logic10"], whatsNew: whatsNew))
            setStatus("Done: 5 installed, 3 updated, 1 already up to date.")
        case "repair":
            let folder = URL(fileURLWithPath: NSHomeDirectory()).appendingPathComponent("Library/Audio/Plug-Ins/VST3")
            issues = [
                RepairIssue(url: folder.appendingPathComponent(".vfd-suite-transaction-5E2C0F1A-8D7B-4C55-9E0F-1B2C3D4E5F60"),
                            kind: .suiteTransaction(state: "committing", safe: false), title: "Interrupted suite installation",
                            detail: "Recorded state: committing. It holds backup copies of plug-ins that were being replaced.", canRestore: true),
                RepairIssue(url: folder.appendingPathComponent(".shifty-stage-0D9E8F7A-6B5C-4D3E-2F1A-0B9C8D7E6F5A"), kind: .singlePluginStage,
                            title: "Temporary copy from a single-plugin update",
                            detail: "Left behind by an interrupted update. It is not your installed plug-in.", canRestore: false),
                RepairIssue(url: URL(fileURLWithPath: NSHomeDirectory()).appendingPathComponent(
                                "Library/Application Support/Vivid Fever Dreams/Recovered/THROW-UP 0.1.5 (saved 2026-10-05 14.02.11).vst3"),
                            kind: .recoveredCopy(ItemKey(plugin: 1, format: .vst3)), title: "Saved previous THROW-UP VST3",
                            detail: "A single-plugin installer saved this copy when an update could not be undone.", canRestore: true)
            ]
            show(.repair)
            renderRepairList()
            setStatus("Choose what to do with each item. Everything you remove goes to the Trash.")
        default:
            setStatus("Quit your audio apps, choose the formats you want, then click Install / Update. Logic Pro and GarageBand use the AU format.")
        }
        guard let content = documentView,
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

    // MARK: Log and window

    static var logURL: URL {
        FileManager.default.homeDirectoryForCurrentUser.appendingPathComponent("Library/Logs/Vivid Fever Dreams Suite/installer.log")
    }
    static func writeLog(_ lines: [String]) {
        // A logging error cannot invalidate a verified install.
        try? InstallLog.append(lines, to: logURL)
    }
    @objc private func viewLog() {
        if SuiteInstallerEngine.exists(Self.logURL) { NSWorkspace.shared.open(Self.logURL) }
        else { setStatus("A log is saved after the first installation attempt.") }
    }
    @objc private func closeWindow() { if !busy { NSApplication.shared.terminate(nil) } }
    func applicationShouldTerminate(_ sender: NSApplication) -> NSApplication.TerminateReply { busy ? .terminateCancel : .terminateNow }
    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool { !busy }
    func windowShouldClose(_ sender: NSWindow) -> Bool { !busy }
}
