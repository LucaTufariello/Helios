import AppKit
import GodEyeCore

/// Opt-in application integration checks. Requires a disposable, explicitly named validation root.
extension ApplicationDelegate {
  func runMissionValidation() {
    guard let rootPath = ProcessInfo.processInfo.environment["GODEYE_VALIDATION_ROOT"],
      URL(fileURLWithPath: rootPath).lastPathComponent.hasPrefix("godeye-mission-validation-"),
      let fixturePath = ProcessInfo.processInfo.environment["GODEYE_VALIDATION_FIXTURES"]
    else {
      print("MISSION_VALIDATION requires an isolated validation root and fixture directory")
      NSApp.terminate(nil)
      return
    }
    model.missionAutomation.stop()
    Task { @MainActor in
      var checks: [String] = []
      let root = URL(fileURLWithPath: rootPath)
      func require(_ value: Bool, _ label: String) throws {
        guard value else { throw SarError.failed(label) }
        checks.append(label)
      }
      do {
        try FileManager.default.createDirectory(at: root, withIntermediateDirectories: true)
        let original = MissionPlan(name: "GENERATED QA — infrastructure review")
        try MissionDocuments.save([original], to: MissionWorkspace.file)
        model.missions.plans = try MissionDocuments.load(from: MissionWorkspace.file)
        model.missions.select(original.id)
        model.missions.objective =
          "Generated fixture: assess a site using dated evidence, notes and review tasks."
        model.missions.notes =
          "QA only. These observations are generated fixtures, not real infrastructure findings."
        try require(model.missions.save(), "mission draft saved")
        model.showSection("Mission planner")
        try require(
          missionWindow != nil && missionWindow !== window, "dedicated native window opened")
        let region = try SentinelRegion(west: 9.16, south: 45.43, east: 9.22, north: 45.49)
        try require(
          model.missions.addArea(
            name: "GENERATED QA area", purpose: "Review boundary", region: region), "area saved")
        model.missionPick = true
        model.beginSarRegionSelection()
        model.missionAreaCapture = true
        try require(!model.missionPick, "region drawing cancels object point selection")
        _ = model.mapClick(at: Coordinate(latitude: region.south, longitude: region.west)!)
        _ = model.mapClick(at: Coordinate(latitude: region.north, longitude: region.east)!)
        try require(
          !model.sarRegionSelecting && missionWindow?.isVisible == true,
          "two map corners return to mission window")
        let area = model.missions.brief.areas[0]
        model.showMissionArea(area)
        try require(
          model.sarSearchRegion == region && abs(model.camera.center.latitude - 45.46) < 0.001,
          "area action controls shared map")
        model.missions.itemName = "GENERATED QA overview"
        model.addMissionView()
        let raw = GeoRecord(
          id: "qa:infrastructure", feed: .installations, label: "GENERATED QA building",
          coordinate: Coordinate(latitude: 45.46, longitude: 9.19)!, altitude: nil,
          observedAt: Date(timeIntervalSince1970: 1000.123456),
          source: "Generated validation fixture", details: ["Type": "Building"])
        var evidence = MissionItem(kind: .record, referenceID: raw.id, title: raw.label)
        evidence.record = MissionRecord(raw)
        model.missions.add(evidence)
        model.openMissionItem(evidence)
        try require(
          model.missionFocus?.coordinate == raw.coordinate && model.selected?.label == raw.label,
          "saved record visible without enabling a live feed")
        var result = try SarProcessor.read(
          SentinelOpticalResult.self,
          from: URL(fileURLWithPath: fixturePath).appendingPathComponent("sentinel2-result.json"))
        result.limitations = ["GENERATED QA preview; not a satellite observation."]
        let id = UUID()
        let directory = root.appendingPathComponent("fixture-optical")
        try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
        let bitmap = NSBitmapImageRep(
          bitmapDataPlanes: nil, pixelsWide: 32, pixelsHigh: 32, bitsPerSample: 8,
          samplesPerPixel: 4, hasAlpha: true, isPlanar: false, colorSpaceName: .deviceRGB,
          bytesPerRow: 0, bitsPerPixel: 0)!
        // Write explicit RGBA bytes: setColor with a monochrome NSColor can leave
        // this RGB bitmap transparent on newer macOS versions.
        guard let pixels = bitmap.bitmapData else {
          throw SarError.failed("Missing fixture bitmap")
        }
        for y in 0..<32 {
          for x in 0..<32 {
            let value: UInt8 = (x / 8 + y / 8) % 2 == 0 ? 64 : 192
            let index = y * bitmap.bytesPerRow + x * 4
            pixels[index] = value
            pixels[index + 1] = value
            pixels[index + 2] = value
            pixels[index + 3] = 255
          }
        }
        try require(
          bitmap.colorAt(x: 0, y: 0)?.alphaComponent == 1, "generated preview fixture is opaque")
        try bitmap.representation(using: .png, properties: [:])!.write(
          to: directory.appendingPathComponent("preview.png"))
        try SatelliteDocuments.save(result, to: directory.appendingPathComponent("result.json"))
        model.optical.images = [SavedOpticalImage(id: id, directory: directory, result: result)]
        model.optical.imageID = id
        model.addMissionOptical()
        let opticalLink = model.missions.plan!.items.first { $0.kind == .optical }!
        model.openMissionItem(opticalLink)
        try require(
          model.rasterMode == "Optical" && model.optical.imageID == id,
          "linked imagery activates map layer")
        let firstTimeline = model.missionTimelineEntries().first { $0.id == opticalLink.id }!
        try require(
          firstTimeline.time == ProviderParsers.date(result.scene!.acquiredAt)
            && opticalLink.acquisition != nil,
          "timeline uses acquisition time and freezes provenance")
        let secondDirectory = root.appendingPathComponent("fixture-optical-later")
        try FileManager.default.createDirectory(
          at: secondDirectory, withIntermediateDirectories: true)
        let laterJSON = try String(
          contentsOf: directory.appendingPathComponent("result.json"), encoding: .utf8
        )
        .replacingOccurrences(of: "20260927", with: "20260928")
        .replacingOccurrences(of: "2026-09-27", with: "2026-09-28")
        try laterJSON.write(
          to: secondDirectory.appendingPathComponent("result.json"), atomically: true,
          encoding: .utf8)
        try FileManager.default.copyItem(
          at: directory.appendingPathComponent("preview.png"),
          to: secondDirectory.appendingPathComponent("preview.png"))
        let later = try SarProcessor.read(
          SentinelOpticalResult.self, from: secondDirectory.appendingPathComponent("result.json"))
        let laterID = UUID()
        model.optical.images.append(
          SavedOpticalImage(id: laterID, directory: secondDirectory, result: later))
        model.optical.imageID = laterID
        model.addMissionOptical()
        let laterLink = model.missions.plan!.items.first { $0.referenceID == laterID.uuidString }!
        try require(
          model.compareMissionTimeline(before: opticalLink.id, after: laterLink.id)
            && model.comparisonLayers.count == 2, "timeline pair activates two shared map layers")
        try require(model.missions.save(), "timeline comparison persists")
        let previewURL = directory.appendingPathComponent("preview.png")
        let previewBytes = try Data(contentsOf: previewURL)
        try FileManager.default.removeItem(at: previewURL)
        let missing = model.missionTimelineEntries().first { $0.id == opticalLink.id }!
        try require(
          !missing.available && missing.time == firstTimeline.time
            && missing.bounds == firstTimeline.bounds,
          "missing raster retains acquisition time and footprint")
        try require(
          !model.compareMissionTimeline(before: opticalLink.id, after: laterLink.id),
          "comparison rechecks deleted preview before map activation")
        try previewBytes.write(to: previewURL)
        try require(
          model.compareMissionTimeline(before: opticalLink.id, after: laterLink.id),
          "comparison recovers after preview restored")
        model.missions.comparisonEnabled = false
        try require(model.missions.save(), "comparison close persists")
        var note = MissionNote(
          title: "GENERATED QA evidence note",
          body: "A review note linked to both a boundary and dated imagery.")
        note.areaID = area.id
        note.evidenceID = opticalLink.id
        try require(model.missions.addNote(note), "linked note persisted")
        model.missionPick = true
        try require(
          model.mapClick(at: raw.coordinate) && model.missionPickedCoordinate == raw.coordinate,
          "map point returned to mission window")
        var object = MissionObservation(
          title: "GENERATED QA warehouse", category: "Building", coordinate: raw.coordinate)
        object.areaID = area.id
        object.evidenceID = opticalLink.id
        object.observedAt = raw.observedAt
        try require(model.missions.addObservation(object), "object identification saved")
        try require(
          model.missions.reviewObservation(
            object.id, state: .uncertain, note: "Needs independent corroboration"),
          "object review persisted")
        model.showObservation(object)
        try require(
          model.missionFocus?.coordinate == object.coordinate
            && model.records.contains { $0.id == model.missionFocus?.id },
          "object shown on shared map")
        let task = MissionTask(
          title: "GENERATED QA verify footprint", due: Date().addingTimeInterval(86400))
        try require(model.missions.addTask(task), "review task saved")
        model.missions.setTask(task.id, done: true)
        try require(model.missions.brief.tasks.first?.done == true, "task completion saved")
        let monitor = MissionMonitor(kind: .evidence)
        try require(model.missions.addMonitor(monitor), "automation created paused")
        model.missionAutomation.start(model: model)
        model.missionAutomation.run(missionID: original.id, monitorID: monitor.id)
        for _ in 0..<40 {
          if !model.missionAutomation.running.contains(monitor.id) { break }
          try await Task.sleep(for: .milliseconds(100))
        }
        try require(
          model.missions.brief.monitors.first?.lastSuccess != nil,
          "evidence automation executed and checkpoint saved")
        let loaded = try MissionDocuments.load(from: MissionWorkspace.file)
        try require(loaded == model.missions.plans, "complete mission round trip")
        try require(
          FileManager.default.fileExists(
            atPath: MissionWorkspace.file.deletingLastPathComponent().appendingPathComponent(
              "plans-before-mission-control.json"
            ).path), "legacy mission backup retained")
        let export = root.appendingPathComponent("mission-export.json")
        try SatelliteDocuments.save(
          model.missions.plan!, to: export, dateEncodingStrategy: .deferredToDate,
          maximumBytes: 8 * 1024 * 1024)
        try require(
          try SarProcessor.read(MissionPlan.self, from: export) == model.missions.plan,
          "mission export round trip")
        let timeline = model.missionTimelineEntries()
        try require(
          timeline.first { $0.id == note.id }?.time == note.createdAt,
          "timeline distinguishes note authoring from image acquisition")
        let csv = MissionTimeline.csv(timeline)
        try csv.write(
          to: root.appendingPathComponent("timeline.csv"), atomically: true, encoding: .utf8)
        try require(
          csv.contains("Note authored") && csv.contains(result.scene!.id),
          "timeline CSV includes times and source identifiers")
        model.missions.setTask(task.id, done: false)
        try require(
          model.missions.brief.tasks.first?.done == false,
          "completed task reopens in overview queue")
        missionWindow?.makeKeyAndOrderFront(nil)
        for section in [
          "Overview", "Areas", "Evidence", "Timeline", "Objects", "Notes", "Tasks", "Automations",
          "Activity",
        ] {
          model.missions.section = section
          try await Task.sleep(for: .milliseconds(250))
          guard let missionWindow else { throw SarError.failed("Missing mission window") }
          let capture = Process()
          capture.executableURL = URL(fileURLWithPath: "/usr/sbin/screencapture")
          capture.arguments = [
            "-x", "-o", "-l", String(missionWindow.windowNumber),
            root.appendingPathComponent("mission-\(section.lowercased()).png").path,
          ]
          try capture.run()
          capture.waitUntilExit()
          guard capture.terminationStatus == 0 else {
            throw SarError.failed(
              "Window screenshot unavailable; screen recording permission may be required")
          }
        }
        checks.append("nine mission sections rendered")
        model.missions.section = "Timeline"
        missionWindow?.setContentSize(NSSize(width: 1000, height: 658))
        try await Task.sleep(for: .milliseconds(250))
        if let missionWindow {
          let capture = Process()
          capture.executableURL = URL(fileURLWithPath: "/usr/sbin/screencapture")
          capture.arguments = [
            "-x", "-o", "-l", String(missionWindow.windowNumber),
            root.appendingPathComponent("mission-timeline-compact.png").path,
          ]
          try capture.run()
          capture.waitUntilExit()
          try require(capture.terminationStatus == 0, "compact mission timeline rendered")
        }
        // Capture the same shared workspace in both appearances and at the minimum width.
        // Generated imagery stays inside the disposable validation library.
        @MainActor func captureWindow(_ target: NSWindow?, named name: String) async throws {
          guard let target else { throw SarError.failed("Missing validation window") }
          target.makeKeyAndOrderFront(nil)
          try await Task.sleep(for: .milliseconds(800))
          let capture = Process()
          capture.executableURL = URL(fileURLWithPath: "/usr/sbin/screencapture")
          capture.arguments = [
            "-x", "-o", "-l", String(target.windowNumber),
            root.appendingPathComponent(name + ".png").path,
          ]
          try capture.run()
          capture.waitUntilExit()
          try require(capture.terminationStatus == 0, name + " rendered")
        }
        for (name, appearance) in [
          ("light", NSAppearance.Name.aqua), ("dark", NSAppearance.Name.darkAqua),
        ] {
          missionWindow?.appearance = NSAppearance(named: appearance)
          window?.appearance = NSAppearance(named: appearance)
          model.missions.section = "Overview"
          missionWindow?.setContentSize(NSSize(width: 1180, height: 800))
          try await captureWindow(missionWindow, named: "design-mission-" + name)
          missionWindow?.setContentSize(NSSize(width: 1000, height: 680))
          try await captureWindow(missionWindow, named: "design-mission-compact-" + name)
          model.panels = []
          model.showSection("Satellite")
          model.satellite.page = "Library"
          window?.setContentSize(NSSize(width: 1360, height: 850))
          try await captureWindow(window, named: "design-library-" + name)
          window?.setContentSize(NSSize(width: 1080, height: 620))
          try await captureWindow(window, named: "design-library-compact-" + name)
        }
        model.openPanel(.map)
        model.focusPanel(.satellite)
        try require(
          model.panels.last == .satellite && Set(model.panels).count == model.panels.count,
          "panel focus preserves one instance per tool")
        model.closePanel(.satellite)
        model.focusPanel(.satellite)
        try require(
          !model.panels.contains(.satellite), "late panel focus cannot reopen a closed panel")
        model.revealMap?()
        window?.setContentSize(NSSize(width: 1360, height: 850))
        model.showAbout = true
        try await Task.sleep(for: .milliseconds(600))
        try require(window?.attachedSheet != nil, "native About sheet opens")
        try await captureWindow(window?.attachedSheet, named: "native-about")
        model.showAbout = false
        missionWindow?.orderOut(nil)
        model.showSection("Mission planner")
        try require(missionWindow?.isVisible == true, "mission window reopens with existing state")
        model.missions.remove(opticalLink.id)
        try require(
          model.missions.brief.notes.first?.evidenceID == nil
            && model.missions.brief.observations.first?.evidenceID == nil,
          "removing evidence preserves dependent notes and observations")
        try require(
          FileManager.default.fileExists(
            atPath: directory.appendingPathComponent("preview.png").path),
          "removing mission link preserves source file")
        let report: [String: Any] = [
          "passed": true, "checks": checks,
          "uiInteraction": "Programmatic native window/model integration; no pointer automation",
          "detector": "Model inference with a trained detector remains unverified",
        ]
        try JSONSerialization.data(withJSONObject: report, options: [.prettyPrinted, .sortedKeys])
          .write(to: root.appendingPathComponent("validation.json"))
        print("MISSION_VALIDATION passed \(checks.count) checks: \(root.path)")
      } catch {
        let report: [String: Any] = [
          "passed": false, "checks": checks, "error": error.localizedDescription,
        ]
        try? JSONSerialization.data(withJSONObject: report, options: [.prettyPrinted, .sortedKeys])
          .write(to: root.appendingPathComponent("validation.json"))
        print("MISSION_VALIDATION FAILED: \(error)")
      }
      model.missions.pendingForms = false
      model.missions.reviewDrafts = [:]
      model.shutdown()
      NSApp.terminate(nil)
    }
  }
}
