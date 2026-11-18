import AppKit
import HeliosCore
import SwiftUI

/// Plain SwiftUI section containers keep the inspector's accessible hierarchy
/// simple while retaining the app's restrained grouped-control appearance.
struct SatelliteSection<Content: View>: View {
  let title: String
  @ViewBuilder var content: Content
  init(_ title: String, @ViewBuilder content: () -> Content) {
    self.title = title
    self.content = content()
  }
  var body: some View {
    VStack(alignment: .leading, spacing: 10) {
      Text(title).font(.headline)
      content
    }.padding(14).frame(maxWidth: .infinity, alignment: .leading)
      .background(WorkspaceDesign.surface, in: RoundedRectangle(cornerRadius: 10))
      .overlay(RoundedRectangle(cornerRadius: 10).strokeBorder(WorkspaceDesign.border))
  }
}

struct SatellitePanel: View {
  @ObservedObject var model: WorkspaceModel
  @ObservedObject var satellite: SatelliteWorkspace
  @ObservedObject var sar: SarWorkspace
  @ObservedObject var optical: OpticalWorkspace
  var body: some View {
    VStack(alignment: .leading, spacing: 0) {
      Picker("Satellite tool", selection: $satellite.page) {
        Text("Library").tag("Library")
        Text("SAR").tag("SAR workflow")
        Text("Optical").tag("Reference imagery")
        Text("Site review").tag("Infrastructure review")
      }.pickerStyle(.segmented).labelsHidden().padding(14)
      if satellite.page != "Infrastructure review" && satellite.page != "Library" {
        Picker("Satellite source", selection: $optical.source) {
          ForEach(SatelliteSource.allCases, id: \.self) { Text($0.name).tag($0) }
        }.padding(.horizontal, 16).padding(.vertical, 10).disabled(optical.busy || sar.busy)
      }
      Text(workflowDescription).font(.caption).foregroundStyle(.secondary)
        .padding(.horizontal, 16).padding(.bottom, 12)
      Divider()
      ScrollView {
        VStack(alignment: .leading, spacing: 16) {
          switch satellite.page {
          case "Library":
            SatelliteLibraryView(
              model: model, satellite: satellite, optical: optical, sar: sar,
              missions: model.missions)
          case "Infrastructure review":
            InfrastructureReviewView(model: model, satellite: satellite, sar: sar)
          case "Reference imagery": referenceImagery
          default: SarTools(sar: sar, model: model)
          }
          if let error = satellite.storageError {
            Text(error).font(.caption).foregroundStyle(.orange)
          }
        }.padding(16).frame(maxWidth: .infinity, alignment: .leading)
      }
    }
    .onAppear {
      if satellite.page == "SAR workflow" {
        optical.source = .sentinel1
        model.rasterMode = "SAR"
      }
      if satellite.page == "Reference imagery" { model.rasterMode = "Optical" }
      if satellite.page == "Infrastructure review" { model.rasterMode = "Review" }
    }
    .onChange(of: model.dailyImagery) { _, value in
      if value != .off { optical.showOverlay = false }
    }
    .onChange(of: satellite.siteID) { _, _ in
      if let site = satellite.site { model.setSarSearchRegion(site.region) }
    }
    .onChange(of: optical.source) { _, value in
      optical.selectSource()
      model.rasterMode = value == .sentinel1 ? "SAR" : "Optical"
      satellite.page = value == .sentinel1 ? "SAR workflow" : "Reference imagery"
      model.dailyImagery = .off
    }
    .onChange(of: satellite.page) { _, _ in
      if satellite.page == "SAR workflow" { model.rasterMode = "SAR" }
      if satellite.page == "Reference imagery" { model.rasterMode = "Optical" }
      if satellite.page == "Infrastructure review" { model.rasterMode = "Review" }
      if satellite.page == "SAR workflow", optical.source != .sentinel1 {
        optical.source = .sentinel1
      }
      if satellite.page == "Reference imagery", optical.source == .sentinel1 {
        optical.source = .sentinel2
      }
      if satellite.page != "Infrastructure review" { model.reviewOutline = [] }
    }
  }
  private var workflowDescription: String {
    switch satellite.page {
    case "Library": "Your saved places and acquisitions. Open an item to work with it on the map."
    case "Reference imagery":
      "Find a dated optical acquisition, view it on the map, and retain its source information."
    case "Infrastructure review":
      "Compare compatible radar acquisitions over a saved site and review measured changes."
    default: "Find, download, and calibrate Sentinel-1 radar products."
    }
  }
  private var referenceImagery: some View {
    VStack(alignment: .leading, spacing: 14) {
      SentinelOpticalView(model: model, optical: optical, sar: sar)
      Divider()
      Text("Basemap context").font(.headline)
      Text(
        "The Esri satellite basemap is a mosaic with varying acquisition dates. Use it for geographic context; it cannot establish when an infrastructure change occurred."
      )
      .font(.callout).foregroundStyle(.secondary)
      Button("Show detailed basemap context") {
        model.basemap = .satellite
        sar.overlayEnabled = false
        satellite.showOverlay = false
        optical.showOverlay = false
      }
      SatelliteSection("Daily satellite imagery") {
        VStack(alignment: .leading, spacing: 8) {
          Picker("Product", selection: $model.dailyImagery) {
            ForEach(DailyImagery.allCases, id: \.self) { Text($0.name).tag($0) }
          }
          if model.dailyImagery != .off {
            Text("Requested date: \(DailyImagery.date()) UTC")
            Text(model.dailyImagery.resolution + " · not live")
            Text(
              "Daily composites may contain gaps; their resolution is unsuitable for individual infrastructure features."
            )
            Slider(value: $model.imageryOpacity, in: 0...1) { Text("Opacity") }
            Text("NASA Worldview / GIBS · daily composite")
          }
        }.font(.caption).padding(6)
      }
      Text(
        "Record dated reference imagery or documents in each candidate’s evidence field. Undated basemap imagery is not a verification source for change timing."
      )
      .font(.caption).foregroundStyle(.secondary)
    }
  }
}

struct InfrastructureReviewView: View {
  @ObservedObject var model: WorkspaceModel
  @ObservedObject var satellite: SatelliteWorkspace
  @ObservedObject var sar: SarWorkspace
  @State private var draft = CandidateReview()
  private var dirty: Bool {
    let saved = satellite.notes?.candidates[satellite.candidateID ?? ""] ?? CandidateReview()
    return draft.note != saved.note || draft.reference != saved.reference
      || draft.decision != saved.decision
  }
  private var completed: [SavedSarJob] { sar.jobs.filter { $0.result.state == .completed } }
  private var pairIssue: String? {
    guard let before = completed.first(where: { $0.id == satellite.beforeID }),
      let after = completed.first(where: { $0.id == satellite.afterID })
    else { return "Select a baseline and a later completed acquisition." }
    return ChangeReviewCompatibility.issue(
      before: before.result, after: after.result, polarization: satellite.options.polarization)
  }
  var body: some View {
    VStack(alignment: .leading, spacing: 16) {
      siteTools
      pairTools
      if satellite.busy {
        if let fraction = satellite.fraction {
          ProgressView(value: fraction)
        } else {
          ProgressView()
        }
        Button("Cancel comparison") { satellite.cancel() }
      }
      Text(satellite.status).font(.callout)
      if let error = satellite.error {
        Text(error).foregroundStyle(.orange).textSelection(.enabled)
      }
      Divider()
      savedReviews
      if let review = satellite.review, review.result.state == .completed {
        reviewResults(review)
      }
    }
    .onChange(of: satellite.candidateID) { _, _ in loadDraft() }
    .onChange(of: satellite.reviewID) { _, _ in
      loadDraft()
      if satellite.review?.result.state == .completed {
        model.dailyImagery = .off
        satellite.showOverlay = true
      }
    }
    .onAppear { loadDraft() }
    .onDisappear {
      if dirty, draft.isValid { satellite.saveDecision(draft) }
    }
  }
  private var siteTools: some View {
    SatelliteSection("1 · Site boundary") {
      VStack(alignment: .leading, spacing: 10) {
        Picker("Site", selection: $satellite.siteID) {
          Text("Select saved site").tag(UUID?.none)
          ForEach(satellite.sites) { Text($0.name).tag(Optional($0.id)) }
        }
        if let site = satellite.site {
          Text("\(site.kind) · \(site.region.summary)").font(.caption)
          Button("Focus saved site") {
            model.setSarSearchRegion(site.region)
            model.focusSatelliteBounds([
              site.region.west, site.region.south, site.region.east, site.region.north,
            ])
          }
          Button("Delete saved site…", role: .destructive) { satellite.deleteSite(site.id) }
        }
        DisclosureGroup("Save a new site / rename selected site") {
          VStack(alignment: .leading, spacing: 8) {
            TextField("Site name", text: $satellite.siteName).textFieldStyle(.roundedBorder)
            Picker("Type", selection: $satellite.siteKind) {
              ForEach(
                [
                  "Infrastructure", "Industrial site", "Port", "Airport", "Rail / logistics",
                  "Energy facility", "Construction site",
                ], id: \.self
              ) { Text($0).tag($0) }
            }
            HStack {
              Button("Draw boundary") { model.beginSarRegionSelection() }
              Button("Use current view") { model.setSarRegionToView() }
            }
            if let region = model.sarSearchRegion {
              Text("New boundary: \(region.summary)").font(.caption)
            }
            HStack {
              Button("Save new site") { satellite.saveSite(region: model.sarSearchRegion) }
                .disabled(
                  model.sarSearchRegion == nil
                    || satellite.siteName.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty)
              Button("Rename selected") { satellite.updateSiteName() }
                .disabled(
                  satellite.site == nil
                    || satellite.siteName.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty)
            }
            Text(
              "Zoom to a site a few kilometres across. Drawing changes the new boundary; saved reviews retain their original boundaries."
            )
            .font(.caption).foregroundStyle(.secondary)
          }.padding(.top, 8)
        }
      }.padding(6)
    }.disabled(satellite.busy || dirty)
  }
  private var pairTools: some View {
    SatelliteSection("2 · Acquisition pair") {
      VStack(alignment: .leading, spacing: 10) {
        if completed.count < 2 {
          Text(
            "Two processed acquisitions are required. Find and process a second date over this site in SAR workflow, using the same satellite, relative orbit and settings."
          )
          .font(.callout)
          Button("Open SAR workflow") { satellite.page = "SAR workflow" }
        }
        Picker("Baseline", selection: $satellite.beforeID) {
          Text("Select earlier date").tag(String?.none)
          ForEach(completed) { Text(acquisitionLabel($0)).tag(Optional($0.id)) }
        }
        Picker("Later", selection: $satellite.afterID) {
          Text("Select later date").tag(String?.none)
          ForEach(completed) { Text(acquisitionLabel($0)).tag(Optional($0.id)) }
        }
        Picker("Polarization", selection: $satellite.options.polarization) {
          Text("VV").tag("VV")
          Text("VH").tag("VH")
        }.pickerStyle(.segmented)
        DisclosureGroup("Change measurement settings") {
          VStack(alignment: .leading, spacing: 8) {
            HStack {
              Text("Candidate threshold (dB)")
              TextField("dB", value: $satellite.options.thresholdDb, format: .number).frame(
                width: 70)
            }
            Picker("Local power mean", selection: $satellite.options.averagingWindow) {
              ForEach([3, 5, 7], id: \.self) { Text("\($0) × \($0) pixels").tag($0) }
            }
            HStack {
              Text("Minimum connected pixels")
              TextField("Pixels", value: $satellite.options.minimumPixels, format: .number).frame(
                width: 70)
            }
            Text(
              "Default: ≥3 dB change in either direction, at least 25 connected pixels. Local means reduce speckle at the cost of detail. These are review thresholds, not probabilities or event classifications."
            )
            .font(.caption).foregroundStyle(.secondary)
          }.padding(.top, 8)
        }
        if let issue = pairIssue { Text(issue).font(.caption).foregroundStyle(.secondary) }
        Button("Compare site acquisitions") {
          model.dailyImagery = .off
          model.orient(heading: 0, tilt: 0)
          satellite.start(sar: sar)
        }.buttonStyle(.borderedProminent)
          .disabled(
            satellite.busy || sar.busy || satellite.site == nil || pairIssue != nil
              || !satellite.options.isValid)
        Text(
          "Same geometry and settings are required. Grid alignment is resampling, not independent validation of geolocation. Moisture, vegetation and viewing effects can also produce changes."
        )
        .font(.caption).foregroundStyle(.secondary)
      }.padding(6)
    }.disabled(satellite.busy || dirty)
  }
  private var savedReviews: some View {
    VStack(alignment: .leading, spacing: 8) {
      Text("3 · Saved reviews & evidence").font(.headline)
      if satellite.reviews.isEmpty {
        Text("No saved comparisons yet. Select a site and two acquisition dates above.")
          .font(.caption).foregroundStyle(.secondary)
      } else {
        Picker(
          "Review",
          selection: Binding(get: { satellite.reviewID }, set: { satellite.selectReview($0) })
        ) {
          Text("Select review").tag(UUID?.none)
          ForEach(satellite.reviews) { review in
            Text(
              "\(review.result.site?.name ?? "Interrupted review") · \(review.result.createdAt.prefix(10)) · \(review.result.state.rawValue)"
            ).tag(Optional(review.id))
          }
        }.disabled(satellite.busy || dirty)
      }
      if satellite.review != nil {
        Button("Show local review files") { satellite.reveal() }
      }
    }
  }
  private func reviewResults(_ review: SavedChangeReview) -> some View {
    VStack(alignment: .leading, spacing: 12) {
      Text("Review results · \(review.result.site?.name ?? "Site")").font(.headline)
      if let evidence = review.result.evidence {
        Text(
          "\(evidence[0].metadata.acquisitionStart) UTC → \(evidence[1].metadata.acquisitionStart) UTC"
        )
        .font(.caption).textSelection(.enabled)
        Text(
          "\(review.result.options?.polarization ?? "") · \(evidence[0].metadata.satellite) · \(evidence[0].metadata.orbitDirection) · relative orbit \(evidence[0].metadata.relativeOrbit ?? 0)"
        )
        .font(.caption)
      }
      Text(
        "Common valid coverage: \((review.result.commonValidFraction ?? 0)*100, specifier: "%.1f")% · \(review.result.width ?? 0) × \(review.result.height ?? 0) pixels"
      )
      .font(.caption)
      Picker("Map output", selection: $satellite.view) {
        ForEach(["Before", "After", "Change"], id: \.self) { Text($0).tag($0) }
      }.pickerStyle(.segmented)
      Toggle("Show review overlay", isOn: $satellite.showOverlay)
      Text(
        satellite.view == "Change"
          ? "Orange: stronger return · Blue: weaker return · Gray: little change · Transparent: invalid / insufficient support. ΔdB = 10 log₁₀(later mean power / baseline mean power)."
          : "Before and after use the same 2–98% dB display scale and local-power averaging."
      )
      .font(.caption).foregroundStyle(.secondary)
      Button("Focus review boundary") {
        if let bounds = review.result.bounds { model.focusSatelliteBounds(bounds) }
      }
      let candidates = review.result.candidates ?? []
      HStack {
        Text("Change candidates").font(.headline)
        Spacer()
        Text("\(candidates.count)").foregroundStyle(.secondary)
      }
      if candidates.isEmpty {
        Text(
          "No patches meet these thresholds. This does not establish that the site was unchanged."
        )
        .font(.caption).foregroundStyle(.secondary)
      }
      ScrollView {
        LazyVStack(alignment: .leading, spacing: 6) {
          ForEach(candidates) { candidate in
            Button {
              satellite.candidateID = candidate.id
            } label: {
              HStack {
                Image(
                  systemName: candidate.direction == "increase"
                    ? "arrow.up.right" : "arrow.down.right"
                )
                .foregroundStyle(candidate.direction == "increase" ? .orange : .blue)
                VStack(alignment: .leading, spacing: 3) {
                  Text(
                    "Patch \(candidate.id) · \(candidate.meanDeltaDb, specifier: "%+.1f") dB · ≈\(candidate.approximateAreaM2 / 10_000, specifier: "%.2f") ha"
                  )
                  Text(satellite.notes?.candidates[candidate.id]?.decision.rawValue ?? "Unreviewed")
                    .font(.caption).foregroundStyle(.secondary)
                }
                Spacer()
              }.padding(8).background(
                satellite.candidateID == candidate.id
                  ? Color.accentColor.opacity(0.12) : Color.clear,
                in: RoundedRectangle(cornerRadius: 6))
            }.buttonStyle(.plain).disabled(dirty)
          }
        }
      }.frame(maxHeight: 240)
      if let candidate = satellite.candidate {
        candidateEditor(candidate)
      }
      Divider()
      HStack {
        Button("Export ΔdB GeoTIFF…") { satellite.exportRaster("delta-db.tif") }
        Button("Export PNG…") { satellite.exportRaster(satellite.view + ".png") }
      }.disabled(satellite.exportBusy)
      Button("Export evidence & review decisions…") { satellite.exportEvidence() }.disabled(
        dirty || satellite.notes == nil)
      Text(
        "Outputs remain radar measurements. Independent radiometric and geographic validation is still needed. No building, vehicle, material, damage or operating-status classification is performed."
      )
      .font(.caption).foregroundStyle(.secondary)
    }
  }
  private func candidateEditor(_ candidate: ChangeCandidate) -> some View {
    SatelliteSection("Review patch \(candidate.id)") {
      VStack(alignment: .leading, spacing: 8) {
        Text(
          "\(candidate.pixels) connected pixels · mean \(candidate.meanDeltaDb, specifier: "%+.2f") dB · peak absolute \(candidate.maximumAbsoluteDeltaDb, specifier: "%.2f") dB"
        )
        .font(.caption)
        Button("Locate patch on map") {
          model.reviewOutline =
            (try? SentinelRegion(
              west: candidate.bounds[0], south: candidate.bounds[1], east: candidate.bounds[2],
              north: candidate.bounds[3]))?.outline ?? []
          model.focusSatelliteBounds(candidate.bounds)
        }
        Text("The cyan rectangle marks patch bounds, not a detected building outline.")
          .font(.caption).foregroundStyle(.secondary)
        Picker("Decision", selection: $draft.decision) {
          ForEach(ChangeDecision.allCases, id: \.self) { Text($0.rawValue).tag($0) }
        }
        Text("Analyst notes").font(.caption.bold())
        TextEditor(text: $draft.note).frame(height: 90).border(Color.secondary.opacity(0.2))
        TextField("Dated reference URL / document citation", text: $draft.reference).textFieldStyle(
          .roundedBorder)
        HStack {
          Button("Save decision") {
            satellite.saveDecision(draft)
          }.disabled(!draft.isValid || satellite.notes == nil)
          if dirty {
            Button("Discard edits") { loadDraft() }
            Text("Unsaved edits").font(.caption).foregroundStyle(.secondary)
          }
        }
        Text(
          "A decision records your interpretation, not an automated claim of confirmed construction."
        )
        .font(.caption).foregroundStyle(.secondary)
      }.padding(6)
        .onChange(of: draft.note) { _, value in
          if value.count > 4000 { draft.note = String(value.prefix(4000)) }
        }
        .onChange(of: draft.reference) { _, value in
          if value.count > 1000 { draft.reference = String(value.prefix(1000)) }
        }
    }
  }
  private func loadDraft() {
    draft = satellite.notes?.candidates[satellite.candidateID ?? ""] ?? CandidateReview()
    model.reviewOutline = []
  }
  private func acquisitionLabel(_ job: SavedSarJob) -> String {
    guard let p = job.result.metadata else { return job.result.inputName }
    return
      "\(p.acquisitionStart) · \(p.satellite) · orbit \(p.relativeOrbit.map(String.init) ?? "?")"
  }
}

struct SatelliteLibraryView: View {
  @ObservedObject var model: WorkspaceModel
  @ObservedObject var satellite: SatelliteWorkspace
  @ObservedObject var optical: OpticalWorkspace
  @ObservedObject var sar: SarWorkspace
  @ObservedObject var missions: MissionWorkspace
  @State private var query = ""
  private func matches(_ text: String) -> Bool {
    let term = query.trimmingCharacters(in: .whitespacesAndNewlines)
    return term.isEmpty || text.localizedCaseInsensitiveContains(term)
  }
  private var sites: [InfrastructureSite] {
    satellite.sites.filter { matches($0.name + " " + $0.kind) }
  }
  private var images: [SavedOpticalImage] {
    optical.images.filter { matches($0.title + " " + ($0.result.scene?.id ?? "")) }
  }
  private var jobs: [SavedSarJob] {
    sar.jobs.filter { matches($0.title + " " + $0.result.state.rawValue) }
  }
  private func acquisitionDate(_ image: SavedOpticalImage) -> String {
    guard let value = image.result.scene?.acquiredAt, let date = ProviderParsers.date(value) else {
      return "Acquisition date unavailable"
    }
    let formatter = DateFormatter()
    formatter.locale = Locale(identifier: "en_US_POSIX")
    formatter.timeZone = TimeZone(secondsFromGMT: 0)
    formatter.dateFormat = "d MMM yyyy · HH:mm 'UTC'"
    return formatter.string(from: date)
  }
  private func imageDetails(_ image: SavedOpticalImage) -> String {
    let result = image.result
    var detail =
      "\((result.mode ?? "RGB").uppercased()) · \(result.width ?? 0) × \(result.height ?? 0) px"
    if let region = result.region {
      detail += String(
        format: " · %.4f°, %.4f°", (region.south + region.north) / 2,
        (region.west + region.east) / 2)
    }
    return detail
  }
  var body: some View {
    LazyVStack(alignment: .leading, spacing: 12) {
      if ProcessInfo.processInfo.environment["HELIOS_VALIDATION_ROOT"] != nil {
        Text("Developer validation profile · isolated test library").font(.caption).foregroundStyle(
          .orange)
      }
      HStack {
        Image(systemName: "magnifyingglass").foregroundStyle(.secondary)
        TextField("Search places, sensors, dates…", text: $query).textFieldStyle(.plain)
          .accessibilityIdentifier("satellite-library-search")
        if !query.isEmpty {
          Button {
            query = ""
          } label: {
            Image(systemName: "xmark.circle.fill")
          }
          .buttonStyle(.plain).accessibilityLabel("Clear library search")
        }
      }.padding(10).background(WorkspaceDesign.surface, in: RoundedRectangle(cornerRadius: 8))
        .overlay(RoundedRectangle(cornerRadius: 8).strokeBorder(WorkspaceDesign.border))
      HStack {
        Button("Find optical imagery", systemImage: "photo") {
          satellite.page = "Reference imagery"
        }
        Button("Open SAR workflow", systemImage: "antenna.radiowaves.left.and.right") {
          satellite.page = "SAR workflow"
        }
      }.controlSize(.small)
      if sites.isEmpty && images.isEmpty && jobs.isEmpty && !query.isEmpty {
        ContentUnavailableView.search(text: query)
      }
      WorkspaceSectionHeading(title: "Saved sites", count: sites.count)
      if satellite.sites.isEmpty {
        HStack {
          Text("No saved site boundaries yet.").font(.callout).foregroundStyle(.secondary)
          Spacer()
          Button("Create a site", systemImage: "plus") { satellite.page = "Infrastructure review" }
        }
      }
      ForEach(sites) { site in
        HStack(spacing: 12) {
          Image(systemName: "mappin.and.ellipse").font(.title3).foregroundStyle(.tint).frame(
            width: 28)
          Button {
            satellite.siteID = site.id
            satellite.page = "Infrastructure review"
            model.setSarSearchRegion(site.region)
            model.focusSatelliteBounds([
              site.region.west, site.region.south, site.region.east, site.region.north,
            ])
          } label: {
            VStack(alignment: .leading, spacing: 4) {
              Text(site.name).fontWeight(.medium)
              Text(site.kind).font(.caption).foregroundStyle(.secondary)
            }.frame(maxWidth: .infinity, alignment: .leading).contentShape(Rectangle())
          }.buttonStyle(.plain).help("Open site on map")
          Button {
            satellite.siteID = site.id
            model.addMissionSite()
          } label: {
            Image(systemName: "link")
          }
          .disabled(missions.plan == nil).help("Link site to selected mission")
          Button(role: .destructive) {
            satellite.deleteSite(site.id)
          } label: {
            Image(systemName: "trash")
          }
          .disabled(satellite.busy).accessibilityLabel("Delete saved site \(site.name)")
        }.padding(12).background(WorkspaceDesign.surface, in: RoundedRectangle(cornerRadius: 8))
      }
      Divider()
      WorkspaceSectionHeading(title: "Optical imagery", count: images.count)
      if optical.images.isEmpty {
        Text("Saved optical acquisitions will appear here with a preview and acquisition date.")
          .font(.callout).foregroundStyle(.secondary)
      }
      ForEach(images) { image in
        Button {
          optical.source = image.result.scene?.sensor ?? .sentinel2
          optical.imageID = image.id
          optical.showOverlay = true
          satellite.page = "Reference imagery"
          if let bounds = image.result.bounds { model.focusSatelliteBounds(bounds) }
        } label: {
          HStack(spacing: 12) {
            MissionImagePreview(
              url: image.directory.appendingPathComponent("preview.png"), maximumPixelSize: 160,
              compact: true
            )
            .frame(width: 80, height: 72).background(.black.opacity(0.08))
            .clipShape(RoundedRectangle(cornerRadius: 6))
            VStack(alignment: .leading, spacing: 5) {
              Text(image.result.scene?.sensor.shortName ?? "Optical image").fontWeight(.semibold)
              Text(acquisitionDate(image)).font(.caption).foregroundStyle(.secondary)
              Text(imageDetails(image)).font(.caption2).foregroundStyle(.secondary).lineLimit(2)
            }.frame(maxWidth: .infinity, alignment: .leading)
            Image(systemName: "arrow.up.right").foregroundStyle(.secondary)
          }.padding(10).background(WorkspaceDesign.surface, in: RoundedRectangle(cornerRadius: 8))
            .contentShape(Rectangle())
        }.buttonStyle(.plain).help("View this acquisition on the map")
      }
      Divider()
      WorkspaceSectionHeading(title: "Radar processing", count: jobs.count)
      if sar.jobs.isEmpty {
        Text("Processed radar acquisitions and job status will appear here.").font(.callout)
          .foregroundStyle(.secondary)
      }
      ForEach(jobs) { job in
        Button {
          sar.selectedJob = job.id
          sar.selectedOutput = job.result.outputs.first?.id ?? "VV"
          sar.workflow = "Process & results"
          satellite.page = "SAR workflow"
          if let bounds = sar.overlay?.bounds { model.focusSatelliteBounds(bounds) }
        } label: {
          HStack(spacing: 12) {
            Image(systemName: "waveform.path").font(.title3).foregroundStyle(.tint).frame(width: 28)
            VStack(alignment: .leading, spacing: 5) {
              Text(job.title).fontWeight(.medium).lineLimit(3)
              Text(job.result.state.rawValue.capitalized).font(.caption).foregroundStyle(.secondary)
            }.frame(maxWidth: .infinity, alignment: .leading)
            Image(systemName: "arrow.up.right").foregroundStyle(.secondary)
          }.padding(12).background(WorkspaceDesign.surface, in: RoundedRectangle(cornerRadius: 8))
            .contentShape(Rectangle())
        }.buttonStyle(.plain)
      }
      if let error = satellite.error { Text(error).font(.caption).foregroundStyle(.orange) }
    }
  }
}
