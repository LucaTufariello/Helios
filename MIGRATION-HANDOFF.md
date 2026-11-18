# Helios: instructions for completing the native macOS/Linux migration

Prepared 15 October 2026. Read this file before editing the project. It is a
repository handoff for a coding model running through an API; no previous chat,
Codex desktop tools, private memory or local Mac build artifacts are required.
All paths below are relative to the repository root unless stated otherwise.

## Assignment and completion boundary

Complete the transition from the existing Swift macOS application to a native
**C++20 / Qt 6 desktop application usable on Apple Silicon macOS and Linux**.
Preserve the existing useful workflows, saved documents, scientific products and
professional map-first interface. Implement and test the missing workflows;
producing another plan or a successful empty window does not complete the task.

The first Linux target is **Omarchy / Arch Linux / Hyprland**, with an **NVIDIA
GeForce GTX 1070**. The user expects future NVIDIA GPUs and support for AMD users.
The Mac used for development is an Apple M2 with 8 GB RAM. Inspect the actual host
and available resources instead of assuming either machine's configuration.

The longer-term goal is a free, open-source defensive intelligence and
infrastructure analysis tool inspired by Maven. Its central workflow is:

**Asset / area → baseline → monitoring → new evidence → reviewed finding → task → report.**

Finish and report the migration acceptance gates in this file first. The user
will then authorize continuation through [docs/ROADMAP.md](docs/ROADMAP.md).
Future roadmap features must not replace missing migration work. Migration
completion does not imply that the larger product goal, scientific validation,
or every planned roadmap milestone has been achieved.

## Starting state: verify before relying on it

The baseline commit is `f40d08a` (`feat: add cross-platform desktop preview and
retire web client`). The repository was clean at that commit when this handoff
was prepared. This handoff itself is a later documentation change. Check the
current branch, commit, status and any intervening changes on the Linux machine.
Remote publication was left to the user; do not assume an older clone contains
this work. The retired web implementation remains in Git history only.

There are currently two clients:

| Location | Current role |
| --- | --- |
| `native/macos/` | Full existing SwiftUI/AppKit application, Metal renderer, Cesium Native bridge, providers, satellite workers and mission workflows. This is the behavioral reference, including its documented limitations. |
| `native/shared/` | Portable C++20 spatial library, built by CMake and SwiftPM from the same source. Used by the existing Swift app and new Qt client. |
| `native/desktop/` | Working but partial Qt Widgets client: GPU map/terrain, completed satellite-product import/view/export and basic mission editing. It is not yet a replacement for the full app. |

The Qt client currently has real public Esri/OSM imagery and Terrarium terrain,
native GPU rendering, camera navigation/tilt, rendered-surface picking, bounded
preview overlays, A/B opacity blending, and a separate Mission Control window.
It reads existing mission JSON while preserving extension fields, and imports
completed optical/SAR `result.json` products. It does **not** yet provide the
full catalogue/download/processing workflows, photorealistic 3D Tiles, remaining
domain feeds, mission timeline/monitoring parity or model inference.

Historical Mac evidence from 13 October 2026:

- Shared spatial suite: 1,885 checks passed. Qt core suite reported 13 passed,
  one opt-in check skipped; the real mission-library check passed separately.
- Swift suite: 178 tests, 10 skipped, zero failures, including the Qt-authored
  mission fixture's Swift read/write compatibility.
- Native Metal harness: 29 generated integration checks; 36 checks with an
  existing SAR product; 32 with an existing optical product; 15 live map/terrain
  checks. These overlap and are not a count of unique tests.
- Core AddressSanitizer/UndefinedBehaviorSanitizer runs passed. Leak detection
  was disabled; this does not establish long-running GPU memory behavior.
- Packaged arm64 app, signature and clean-source Mac build passed.

These are historical results, **not evidence that Linux works**. QA output under
`output/` is ignored by Git and may not be present on the PC. Rerun relevant tests
and record fresh evidence. The configured Arch CI job is not proof it has run.
The interrupted work immediately before this handoff only inspected source;
no new processing-center implementation was started.

## Product and architecture constraints

1. Use compiled C++20 and Qt 6 Widgets. No Electron, WebKit, Qt WebEngine, web UI,
   browser renderer, JavaScript runtime or local web server for the desktop UI.
2. Keep Metal on macOS and Vulkan on Linux behind the rendering boundary. The
   current implementation uses Qt RHI private APIs; compile and deploy matching
   Qt libraries/headers. Qt 6.11.2 was tested on the Mac; CMake requires 6.8+.
   Verify compatibility with the installed version instead of assuming it.
3. Keep scientific processing behind a process/service abstraction. Reuse the
   existing Python/Rasterio/GDAL/NumPy/Pillow/h5py workers and SNAP GPT. Do not
   replace calibration or terrain correction with visual approximations.
4. Keep CUDA optional. Map rendering, documents and ordinary startup must work
   without it. The GTX 1070 is Pascal; CUDA 13 dropped Pascal compilation/library
   support. Recheck maintained compatibility documentation before selecting a
   compute runtime, and verify that chosen packages actually contain compatible
   kernels. A new driver alone does not prove inference support. AMD acceleration
   is a separately tested backend; retain a CPU baseline. No driver changes or
   large model downloads should happen as a side effect of opening the app.
5. Keep the map as the main view with horizontal Map/Satellite/Marine/Sky/Ground
   sections, floating tools, and a separate Mission Control window whose actions
   affect that same map. Preserve readable compact layouts, dark/light appearance,
   keyboard access and explicit loading/empty/error states.
6. Preserve source identifiers, UTC acquisition times, units, nodata, CRS, original
   scientific values and processing provenance. Preview pixels, raw measurements,
   model proposals and analyst judgments remain distinct.
7. Preserve user data and unknown document fields. Use backups, bounded parsing,
   atomic saves and conflict checks. Never silently reset an unreadable library.
   Work with disposable profiles or copies during migration tests.
8. Do not restore bike share, radio, cockpit, voice or San Francisco neighborhood
   boundaries. Their removal was intentional. Direct ASI PRISMA ordering was
   explicitly deferred; preserve local L2D import without adding account ordering.
9. Keep real functionality and honest status. Generated fixtures belong only in
   isolated tests. No invented data, readiness scores, progress, georeferencing,
   sensor coverage, detections or validated-construction/damage claims.
10. Preserve the Swift client until the replacement meets parity and both-platform
    acceptance. Do not delete it merely because the Qt build succeeds. Preserve
    existing local edits; commit only when requested and leave pushing to the user
    unless they explicitly authorize it.

## Read and inspect these sources

Start with [README.md](README.md), [docs/CROSS-PLATFORM.md](docs/CROSS-PLATFORM.md),
[docs/CURRENT-STATE.md](docs/CURRENT-STATE.md), [TESTING.md](TESTING.md) and
[native/desktop/README.md](native/desktop/README.md). Then inspect the actual code.
Documentation can lag implementation; resolve discrepancies with tests and runtime
evidence, and record limitations rather than copying unsupported claims.

| Area | Existing implementation / migration entry points |
| --- | --- |
| Build and CI | `CMakeLists.txt`, `Makefile`, `native/desktop/CMakeLists.txt`, `native/desktop/build.sh`, `.github/workflows/ci.yml` |
| Shared geometry | `native/shared/Sources/Spatial.cpp`, its public C header and tests; Swift adapters in `Geography.swift` and `SceneGeometry.swift` |
| Qt renderer | `native/desktop/Graphics/MapWidget.*`, `TileCache.*`, `Core/Geography.*`, `shaders/` |
| Full scene engine | `native/macos/engine/`, `Sources/CScene/`, `Sources/HeliosCore/NativeTileset.swift`, `Terrain.swift`, `SceneGeometry.swift`, `Sources/HeliosMac/GlobeRenderer.swift` |
| Qt mission/product storage | `native/desktop/Core/Missions.*`, `Products.*`, `UI/MissionWindow.*`, `UI/SatelliteWindow.*` |
| Full mission behavior | `native/macos/Sources/HeliosCore/MissionPlan.swift`, `MissionOperations.swift`, `MissionTimeline.swift`; `HeliosMac/Mission*Workspace.swift`, `MissionControlView.swift`, `MissionTimelineView.swift` |
| Sentinel-1 acquisition/processing | `HeliosCore/SentinelCatalog.swift`, `SarProcessor.swift`; `HeliosMac/SarAcquisitionWorkspace.swift`, `SarWorkspace.swift`, `SatelliteViews.swift` |
| Optical acquisition | `HeliosCore/SentinelOptical.swift`; `HeliosMac/OpticalWorkspace.swift`, `SatelliteWorkspace.swift` |
| Scientific workers | `native/macos/Sources/HeliosMac/Resources/SAR/{sentinel1.py,change_review.py}` and `Resources/Optical/{sentinel2.py,optical_sources.py}` |
| Sites/change review | `HeliosCore/InfrastructureReview.swift`, `MappedSites.swift`; satellite workspace and Python change-review worker |
| Sources and map tools | `HeliosCore/NativeSources.swift`, `Transport.swift`, `ProviderParsers.swift`, `Records.swift`, `ReferenceLayers.swift`, domain-specific files, `AnnotationDocument.swift`, `Directions.swift`, `WorkspaceState.swift`; `HeliosMac/WorkspaceModel.swift`, `WorkspaceTools.swift` |
| Credentials/storage | `native/macos/Sources/HeliosMac/LocalStorage.swift`, mission/satellite storage implementations; Qt `main.cpp` and settings/profile handling |
| Local detection | `native/macos/Sources/HeliosMac/MissionDetection.swift`, mission object/provenance models; [detector limits](docs/LOCAL-OBJECT-DETECTION.md) |
| Tests | `native/shared/Tests/`, `native/desktop/Tests/CoreTests.cpp`, `native/desktop/Validation.cpp`, `native/macos/Tests/HeliosCoreTests/`, `native/macos/Tests/SAR/`, `native/macos/Validation/` |

In rows using `HeliosCore/` or `HeliosMac/`, those directories are under
`native/macos/Sources/`. Reuse existing native components where practical; the
Objective-C++ scene bridge itself still needs a portable boundary. Do not rebuild
complex 3D Tiles/SAR algorithms from scratch merely to eliminate a wrapper.

## First Linux session: establish real evidence

Run from the actual repository root. Do not hardcode the previous Mac checkout.
Read any applicable `AGENTS.md` supplied by the new environment. Inspect status
before edits; the normal user/API runner's permissions still govern execution.

```sh
git status --short
git log -3 --oneline
./native/desktop/diagnose-linux.sh
```

Inspect the OS/session, compiler, Qt, NVIDIA and Vulkan diagnostics. If development
dependencies are missing, use Omarchy's normal update process and install a
consistent package set, respecting that environment's installation permissions:

```sh
sudo pacman -S --needed base-devel cmake qt6-base qt6-shadertools qt6-wayland vulkan-tools
make test-shared
make test-desktop
```

`make build`, `make test` and `make setup-raster` currently target the full **Mac**
client. Do not run them blindly on Linux or weaken them to report success there.
The Qt executable on Linux is `output/desktop/build/bin/HeliosDesktop`.

Run GPU checks inside the real graphical session, retaining each run's logs and
reports in a new QA directory. For example:

```sh
helios_run="output/qa/linux-$(date -u +%Y%m%dT%H%M%SZ)"
mkdir -p "$helios_run"
./native/desktop/diagnose-linux.sh > "$helios_run/environment.txt" 2>&1
./output/desktop/build/bin/HeliosDesktop --validate \
  --qa-output "$helios_run/generated" > "$helios_run/generated.log" 2>&1
./output/desktop/build/bin/HeliosDesktop --validate-live \
  --qa-output "$helios_run/live" > "$helios_run/live.log" 2>&1
```

Check each command's exit status, `validation.json`, backend/GPU identity and
actual screenshots. A blank framebuffer, software Vulkan fallback, unavailable
display, stale report or skipped test is not hardware acceptance. If the API
session cannot access the desktop, complete headless work and report the exact
manual/GPU check still needed. Do not repeatedly restart an apparently slow job:
inspect its current process/log/output state first.

The raster suite can run independently of Swift using an explicit Linux Python
environment. The current Mac setup script deliberately rejects non-arm64 hosts;
create a Linux environment instead of removing that guard:

```sh
python3 -m venv .venv
.venv/bin/python3 -m pip install -r native/macos/Sources/HeliosMac/Resources/SAR/requirements.txt
HELIOS_SAR_PYTHON="$PWD/.venv/bin/python3" make test-raster
```

Record actual Python/GDAL/Rasterio versions and dependency installation failures.
SNAP GPT is a separate external dependency needed for real GRD processing, not
for the map or ordinary optical rendering. Never treat fixture tests as a real
SNAP/calibration/DEM-download acceptance run.

## Implementation sequence and acceptance matrix

Create and maintain `docs/MIGRATION-STATUS.md` as work proceeds. Inventory every
existing enabled workflow from the Swift UI, source catalog and tests; the matrix
below is a starting index, not permission to omit a feature absent from this list.
For each item record: reference behavior, Qt status, tests, Linux evidence, Mac
evidence, remaining limitations and next action. Use **missing / partial /
implemented / verified / blocked**, with evidence required for verified status.
Never use a disabled button or preserved JSON field as proof of workflow parity.

### 1. Establish renderer and platform foundations

- Fix Linux compilation, Qt plugins and real Wayland/Vulkan operation first.
- Validate imagery orientation, depth precision near/far, dateline behavior,
  parent/child tile masks, picking, draped geometry, tilt and loaded-terrain
  collision/clearance. Missing terrain must remain explicit.
- Port the current photorealistic 3D Tiles capabilities using the pinned native
  engine where practical. Test actual scene content, selection and resource
  lifetime; preserve current supported/unsupported format boundaries.
- Exercise pan/scroll directions, zoom, keyboard, resize, DPI changes, multiple
  windows/displays, focus, minimize/restore and repeated open/close cycles.
- Measure idle and active CPU/GPU/RAM use with the actual device, repeated travel
  and two overlays. Keep bounded caches/concurrency and avoid continuous idle
  rendering. Do not turn a rendered-height fallback into a navigation guarantee.

### 2. Complete storage, jobs and satellite acquisition

- Migrate site/image/job libraries, settings and safe deletion/recovery. Respect
  the distinction between removing a mission link and deleting its source files.
  Provide clear filenames, sensible import locations and missing-file recovery.
- Add a native background process/job boundary with real stage events, bounded
  logs, cancellation, failure cleanup, elapsed time, preflight and relaunch
  recovery. Invoke executables with argument arrays, never interpolated shells.
  Cancellation must stop the worker and relevant child GPT work without killing
  unrelated processes. Never infer liveness solely from a saved PID.
- Port Sentinel-1 region/date/polarization search, acquisition footprint,
  authenticated download and SAFE/ZIP admission. Preserve source-based filenames,
  error handling and provider identity checks. Apply ZIP traversal/extraction
  bounds, XML restrictions, temporary-directory isolation and storage limits.
- Integrate the existing GRD pipeline: metadata/orbit policy, appropriate border
  and thermal noise removal, Sigma0 calibration, optional Refined Lee, DEM terrain
  correction, float scientific export, dB/display conversion, VV/VH/composite
  preview and metadata. Inspect the worker's actual contract and provenance.
- Port Sentinel-2 region/date/cloud search and windowed optical retrieval;
  CBERS-4A PAN/native RGB; local PRISMA L2D PAN/RGB/spectral selection. Preserve
  real availability limits, source-specific units and no-data behavior. Keep
  ASI account ordering deferred. No optical sharpening claim creates detail.
- Package/discover the shared workers on both platforms without Mac source-path
  assumptions or duplicated algorithms. Keep heavy reads off the UI thread,
  source raster values separate from previews, and bounded window processing.
- Verify search → select → retrieve/process → view → save → reopen → export
  with real identified products. Also test invalid input, unavailable dependency,
  network interruption, cancellation, full/unwritable output and restart.

### 3. Complete infrastructure review and Mission Control parity

- Preserve saved sites, compatible acquisition selection, bounded measured SAR
  change review, review decisions, quality metadata and result exports. Retain
  the current one-million-source-pixel limit until a separately tested extension.
- Port all existing mission editing/deletion, notes and citations, tasks/due dates,
  observations/review rationale, evidence snapshots, saved views/annotations,
  comparison persistence, timeline filters, area gaps and CSV/JSON exports.
- Preserve acquisition time versus authored/retrieved time; unknown stays unknown.
  CSV exports must retain formula-injection escaping and source identifiers.
- Port acquisition/evidence monitors with persisted baselines, deduplication,
  interval/concurrency limits, paused state, errors and sleep/relaunch behavior.
  Current checks run while the app is open; they do not automatically download
  large products and are not an unattended system service.
- Verify actions from Mission Control affect the main map and preserve unsaved
  drafts when switching sections/missions or closing windows. Exercise missing
  evidence, linked deletion and reopening from disk, not just model setters.

### 4. Restore remaining domain and map workflows

- Inventory and port the existing Marine/Sky/Ground source catalog, including
  provider setup, refresh/cancellation, attribution, timestamps and honest stale,
  unavailable or limited-coverage states. Use the existing parsers/tests as the
  reference; optional credentials remain optional.
- Include record selection/inspection, raw data, search/directions, annotations,
  saved map state and existing space/orbit behavior. Propagated orbits remain
  estimates and stored record snapshots remain distinct from live observations.
- Keep network timeouts, response/decode limits, cache budgets, schema validation
  and URL admission. Map-provider access is not an entitlement to bulk downloads.
- Replace platform-specific credential handling with an explicit platform adapter
  (macOS Keychain and an appropriate available Linux secret store). Never silently
  fall back to exporting secrets in JSON/settings/logs. Explain unavailable setup.

### 5. Port local assistance without inventing model capability

- Preserve manual object observations and model provenance/review records.
  Apple Vision/Core ML itself cannot be the Linux implementation; use a replaceable
  native/process inference boundary with an explicit supported model contract.
- Validate a compatible local model on CPU before claiming that path works, then
  test optional NVIDIA acceleration on the actual card/runtime. Core ML can remain
  a macOS backend. A candidate ONNX/other backend is not implemented merely by
  mentioning it. Reject unsupported tensors/classes/rotated boxes explicitly.
- Verify box-to-image-to-map transforms, preprocessing, thresholds, cancellation,
  empty results, memory limits and model/source hashes. Scores remain model scores.
  No real remote-sensing detector was scientifically validated in the old client;
  preserve that limitation. Broader aerial-model benchmarking belongs to the roadmap.

### 6. Package and verify both platforms

- Produce an installable Linux artifact/launcher with required Qt platform and
  image/TLS plugins, worker resources and license notices. Verify launch outside
  the source/build directory and with a clean user profile. Development-machine
  paths or libraries must not be hidden runtime requirements.
- Build the Qt macOS app as arm64 with matching deployed Qt libraries; verify its
  signature and actual Metal behavior. Ad-hoc signing is not notarized distribution.
- Run the relevant Swift compatibility/regression tests on a real Mac or Mac CI.
  Linux-only evidence cannot close the macOS gate. Retain the working Swift client
  until the migration is accepted; removing it is a later explicit cleanup step.
- Check licenses for code, dependencies, datasets and any selected model. Preserve
  notices; repository MIT licensing does not relicense provider data/model weights.

## Data compatibility and required regression cases

Original user data lives outside Git on macOS under
`~/Library/Application Support/Helios/`. In particular:

- `Missions/plans.json`: mission library.
- `Satellite/optical/`: optical job folders.
- `SAR/jobs/`: processed radar job folders.

Do not expect those files on Linux after cloning. Use copied test libraries and
complete job folders with `result.json` and its referenced files. Mission links
do not contain raster bytes. Re-establish library paths on Linux. The Qt profile
uses `QStandardPaths` or `--profile DIRECTORY`; discover the actual location.
Do not edit one live mission file concurrently with Swift and Qt: their locks do
not form a shared-editing protocol.

Required compatibility checks include:

- Swift → Qt → save/reopen → Swift round trips on copies, including unknown fields,
  optional legacy fields, UUID references, review/monitor data and numeric
  Foundation dates (2001 epoch) versus ISO UTC dates.
- Corrupt/oversized JSON, Unicode limits, conflicting writers, interrupted atomic
  saves and missing files preserve originals and give actionable errors.
- SAFE/ZIP/XML/HDF admission, untrusted paths/symlinks, unsafe external references,
  malformed/oversized images and cancellation cleanup remain covered.
- SAR scientific-grid dimensions can differ from the bounded preview dimensions;
  preserve both. Transparent/nodata pixels, pixel-edge bounds and CRS must survive
  viewing/export. Exports retain original bytes/float values where promised.
- Library deletion and mission unlinking obey their distinct scopes; no accidental
  cascade into downloaded source products, scientific rasters or unrelated files.
- Old mission, optical, SAR, infrastructure-review and annotation fixtures remain
  readable. Add tests for migrated behavior; do not delete failing regressions to
  produce a green report. Generated fixtures are not scientific validation data.

For a copied real product on Linux, the existing Qt harness supports:

```sh
./output/desktop/build/bin/HeliosDesktop --validate \
  --product /absolute/path/to/copied-job/result.json \
  --qa-output output/qa/linux-product-check
```

For the opt-in mission core check, use a disposable copy:

```sh
HELIOS_MISSION_CHECK=/absolute/path/to/copied-plans.json \
  ./output/desktop/build/native/desktop/HeliosDesktopTests existingMissionLibraryReadOnlyCheck
```

As new workflows migrate, extend the native integration harness and run ordinary
core/raster tests. Run relevant sanitizers on owned C++ code. Formatting uses
`native/desktop/.clang-format`; Swift formatting/build checks require the Mac
toolchain. Keep CI platform labels and skip reasons truthful.

## Evidence and reporting contract

For each completed workflow, retain the source revision, host/GPU/backend,
dependency versions, commands/exit codes, report paths, inspected screenshots,
data identifiers and limitations. Record measured memory/latency with its scenario
rather than asserting the app is efficient because it compiles. Do not commit
credentials, personal missions, large satellite datasets or generated build output.

Update `docs/MIGRATION-STATUS.md` after each meaningful increment so another API
session can continue without chat history. Suggested row format:

| Workflow | Status | Code/tests | Linux evidence | Mac evidence | Remaining work |
| --- | --- | --- | --- | --- | --- |
| Example workflow | partial | paths/test names | command/report or not run | command/report or not run | concrete next step |

Mark migration complete only when the inventory is accounted for, both clients'
required behavior works in the Qt replacement, old data survives, both OS builds
and real workflows pass, and remaining limitations are explicit and accepted.
An intentional exclusion is not a broken port; an unimplemented existing feature
is not an exclusion unless the user agrees. Missing target hardware, credentials,
real data or GUI access must be reported as a specific unverified gate.

The completion report should list migrated workflows, artifacts/run commands,
tests and skips, Linux/Mac hardware evidence, data compatibility, resource
measurements and unresolved limits. Update current-state/build/handoff docs to
match the result. When the user moves on to the roadmap, resume its acceptance
gates in order; do not equate migration completion with validated SAR changes,
object detection accuracy or the final Maven-inspired product goal.
