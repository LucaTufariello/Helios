import AppKit
import HeliosCore
import MetalKit
import SwiftUI

@MainActor final class ApplicationDelegate: NSObject, NSApplicationDelegate, NSWindowDelegate {
  let model = WorkspaceModel()
  var window: NSWindow?
  var missionWindow: NSWindow?
  func applicationDidFinishLaunching(_ notification: Notification) {
    let window = NSWindow(
      contentRect: NSRect(x: 0, y: 0, width: 1360, height: 850),
      styleMask: [.titled, .closable, .resizable, .miniaturizable], backing: .buffered, defer: false
    )
    window.title = "Helios"
    window.minSize = NSSize(width: 1080, height: 620)
    window.contentView = NSHostingView(rootView: WorkspaceView(model: model))
    window.delegate = self
    window.isReleasedWhenClosed = false
    self.window = window
    model.revealMap = { [weak self] in
      self?.window?.makeKeyAndOrderFront(nil)
      self?.model.setVisible(true)
    }
    model.revealMission = { [weak self] in self?.openMissionWindow() }
    model.missionAutomation.start(model: model)
    window.center()
    window.makeKeyAndOrderFront(nil)
    NSApp.setActivationPolicy(.regular)
    installMenu()
    NSApp.activate(ignoringOtherApps: false)
    if CommandLine.arguments.contains("--mission-validation") { runMissionValidation() }
    if CommandLine.arguments.contains("--native-smoke") {
      model.setEnabled(.earthquakes, true)
      Task { [weak self] in
        try? await Task.sleep(for: .seconds(15))
        guard let self else { return }
        let output: [String: Any] = [
          "gpu": model.renderer?.device.name ?? "unavailable",
          "frames": model.renderer?.renderedFrames ?? 0,
          "loadedTiles": model.renderer?.loadedTiles ?? 0,
          "tileFailures": model.renderer?.tileFailures ?? 0,
          "earthquakes": model.snapshots[.earthquakes]?.records.count ?? 0,
          "feedError": model.errors[.earthquakes] ?? "", "rendererError": model.renderError ?? "",
          "webView": false,
        ]
        if let data = try? JSONSerialization.data(withJSONObject: output, options: [.sortedKeys]),
          let text = String(data: data, encoding: .utf8)
        {
          print(text)
        }
        model.shutdown()
        NSApp.terminate(nil)
      }
    }
  }
  func openMissionWindow() {
    if let missionWindow {
      missionWindow.makeKeyAndOrderFront(nil)
      return
    }
    let mission = NSWindow(
      contentRect: NSRect(x: 0, y: 0, width: 1180, height: 800),
      styleMask: [.titled, .closable, .resizable, .miniaturizable], backing: .buffered, defer: false
    )
    mission.title = "Helios — Mission Control"
    mission.minSize = NSSize(width: 1000, height: 680)
    mission.contentView = NSHostingView(
      rootView: MissionControlView(
        model: model, missions: model.missions,
        optical: model.optical, sar: model.sar, satellite: model.satellite,
        automation: model.missionAutomation, detection: model.missionDetection,
        section: Binding(
          get: { self.model.missions.section }, set: { self.model.missions.section = $0 })))
    mission.delegate = self
    mission.isReleasedWhenClosed = false
    if !CommandLine.arguments.contains("--mission-validation") {
      mission.setFrameAutosaveName("HeliosMissionControl")
    }
    missionWindow = mission
    mission.center()
    mission.makeKeyAndOrderFront(nil)
  }
  func application(_ application: NSApplication, open urls: [URL]) {
    for url in urls { model.open(url) }
  }
  func applicationShouldTerminate(_ sender: NSApplication) -> NSApplication.TerminateReply {
    model.missions.resolveUnsavedChanges() ? .terminateNow : .terminateCancel
  }
  func windowShouldClose(_ sender: NSWindow) -> Bool {
    if sender === window, missionWindow?.isVisible == true { return true }
    return model.missions.resolveUnsavedChanges()
  }
  func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool { true }
  func applicationWillTerminate(_ notification: Notification) { model.shutdown() }
  func windowDidMiniaturize(_ notification: Notification) {
    if notification.object as? NSWindow === window { model.setVisible(false) }
  }
  func windowDidDeminiaturize(_ notification: Notification) {
    if notification.object as? NSWindow === window { model.setVisible(true) }
  }
  private func installMenu() {
    let root = NSMenu()
    let app = NSMenuItem()
    let appMenu = NSMenu()
    app.submenu = appMenu
    appMenu.addItem(withTitle: "About Helios", action: #selector(about), keyEquivalent: "").target =
      self
    appMenu.addItem(.separator())
    appMenu.addItem(
      withTitle: "Hide Helios", action: #selector(NSApplication.hide(_:)), keyEquivalent: "h")
    let hideOthers = appMenu.addItem(
      withTitle: "Hide Others", action: #selector(NSApplication.hideOtherApplications(_:)),
      keyEquivalent: "h")
    hideOthers.keyEquivalentModifierMask = [.command, .option]
    appMenu.addItem(
      withTitle: "Show All", action: #selector(NSApplication.unhideAllApplications(_:)),
      keyEquivalent: "")
    appMenu.addItem(
      withTitle: "Quit Helios", action: #selector(NSApplication.terminate(_:)), keyEquivalent: "q")
    root.addItem(app)
    let edit = NSMenuItem(title: "Edit", action: nil, keyEquivalent: "")
    let editMenu = NSMenu(title: "Edit")
    edit.submenu = editMenu
    editMenu.addItem(withTitle: "Undo", action: NSSelectorFromString("undo:"), keyEquivalent: "z")
    let redo = editMenu.addItem(
      withTitle: "Redo", action: NSSelectorFromString("redo:"), keyEquivalent: "z")
    redo.keyEquivalentModifierMask = [.command, .shift]
    editMenu.addItem(.separator())
    for (name, action, key) in [
      ("Cut", #selector(NSText.cut(_:)), "x"), ("Copy", #selector(NSText.copy(_:)), "c"),
      ("Paste", #selector(NSText.paste(_:)), "v"),
      ("Select All", #selector(NSText.selectAll(_:)), "a"),
    ] { editMenu.addItem(withTitle: name, action: action, keyEquivalent: key) }
    root.addItem(edit)
    let view = NSMenuItem(title: "View", action: nil, keyEquivalent: "")
    let viewMenu = NSMenu(title: "View")
    view.submenu = viewMenu
    viewMenu.addItem(withTitle: "Map", action: #selector(showWorkspace), keyEquivalent: "1")
      .target = self
    viewMenu.addItem(
      withTitle: "Satellite Analysis", action: #selector(showSatellite), keyEquivalent: "2"
    )
    .target = self
    viewMenu.addItem(withTitle: "Marine", action: #selector(showMarine), keyEquivalent: "3")
      .target = self
    viewMenu.addItem(withTitle: "Sky", action: #selector(showSky), keyEquivalent: "4").target = self
    viewMenu.addItem(withTitle: "Ground", action: #selector(showGround), keyEquivalent: "5")
      .target = self
    viewMenu.addItem(
      withTitle: "Mission Control", action: #selector(showMission), keyEquivalent: "6"
    ).target = self
    viewMenu.addItem(withTitle: "Full Globe", action: #selector(fullGlobe), keyEquivalent: "0")
      .target = self
    viewMenu.addItem(withTitle: "Refresh Sources", action: #selector(refresh), keyEquivalent: "r")
      .target = self
    viewMenu.addItem(withTitle: "Copy View Link", action: #selector(copyLink), keyEquivalent: "l")
      .target = self
    let fullScreen = viewMenu.addItem(
      withTitle: "Enter Full Screen", action: #selector(NSWindow.toggleFullScreen(_:)),
      keyEquivalent: "f")
    fullScreen.keyEquivalentModifierMask = [.command, .control]
    root.addItem(view)
    let windowItem = NSMenuItem(title: "Window", action: nil, keyEquivalent: "")
    let windowMenu = NSMenu(title: "Window")
    windowItem.submenu = windowMenu
    windowMenu.addItem(
      withTitle: "Minimize", action: #selector(NSWindow.performMiniaturize(_:)), keyEquivalent: "m")
    windowMenu.addItem(
      withTitle: "Zoom", action: #selector(NSWindow.performZoom(_:)), keyEquivalent: "")
    windowMenu.addItem(.separator())
    windowMenu.addItem(
      withTitle: "Bring All to Front", action: #selector(NSApplication.arrangeInFront(_:)),
      keyEquivalent: "")
    root.addItem(windowItem)
    NSApp.windowsMenu = windowMenu
    NSApp.mainMenu = root
  }
  @objc private func showWorkspace() { model.showSection("Map") }
  @objc private func showSatellite() { model.showSection("Satellite") }
  @objc private func showMarine() { model.showSection("Marine") }
  @objc private func showSky() { model.showSection("Sky") }
  @objc private func showGround() { model.showSection("Ground") }
  @objc private func showMission() { model.showSection("Mission planner") }
  @objc private func fullGlobe() { model.resetGlobe() }
  @objc private func refresh() { model.refresh() }
  @objc private func copyLink() { model.copyLink() }
  @objc private func about() { model.showAbout = true }
}

let application = NSApplication.shared
let delegate = ApplicationDelegate()
application.delegate = delegate
application.run()
