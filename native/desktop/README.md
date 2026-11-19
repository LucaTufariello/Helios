# Helios C++ / Qt desktop client

This compiled C++20/Qt client is the Helios desktop application on macOS and
Linux. The earlier Swift client has been removed. Current evidence and remaining
acceptance gates are recorded in [migration status](../../docs/MIGRATION-STATUS.md).

## Implemented

- A shared native analyst design with domain navigation, floating map palettes,
  compact-window layouts and a Mission Control sidebar. See the
  [desktop design guide](../../docs/DESKTOP-DESIGN.md).
- Area dossiers with curated evidence, pinned reference acquisitions, scoped reviews,
  notes/tasks and existing map/Timeline/SAR handoffs. See [site dossiers](../../docs/SITE-DOSSIERS.md).
- Site watches, an inbox, translation-only registration with independent controls,
  and bounded local SAR analysis through the shared scientific queue.
  See [site monitoring](../../docs/SITE-MONITORING.md) for optional dependencies and limits.
- Qt Widgets map workspace and a separate Mission Control window sharing one model.
- GPU rendering through Qt RHI: Metal on the tested Mac, Vulkan verified on the
  Omarchy/GTX 1070 with generated imagery and live public map/terrain checks.
- Shared pinned Cesium engine for local/same-origin HTTPS triangle photogrammetry,
  glTF materials/textures, rendered-mesh picking, bounded GPU resources and credits.
  Generated and real public AGI headquarters content have rendered on the GTX 1070.
- WGS84 globe, public Esri/OSM imagery, Terrarium terrain, reverse depth, local mesh
  origins, terrain-relative camera clearance and picking of rendered triangles.
- Parent imagery masked only where actual child imagery has loaded; picking uses
  the same masks. Missing terrain remains a labeled fallback.
- Bounded network/cache/decode queues; tile and product decoding off the UI thread;
  map rendering on demand rather than an idle animation loop.
- Existing completed optical/SAR product manifest import, bounded previews,
  georeferenced terrain-draped overlays, pixel-grid display and two-layer blending.
- Raster overlays reuse the map's surface grid, with one shared GPU texture per
  active image rather than a texture copy for each terrain tile.
- Separate scientific-grid/preview dimensions and original-file exports with
  bounded copying, cancellation and atomic destination replacement.
- Existing mission JSON read/write with preserved extension fields, Foundation
  date compatibility, bounded validation, atomic saves and concurrent-change checks.
- Mission briefs, areas, saved views, evidence links, notes, tasks, manual observations,
  review decisions, source inspection and bounded activity history.
- Timeline filters, acquisition gaps, persisted A/B comparisons, CSV/JSON exports
  and in-app acquisition/evidence monitors with baselines, deduplication and pause.
- Native Map/Satellite/Marine/Sky/Ground sections and a Sources & observations
  palette covering all 19 enabled layers, source inspection, attribution, retained
  fixes, estimated orbits, map selection and saved mission snapshots.
- Bounded native camera still/clip playback using an owned FFmpeg process; optional
  provider keys use the operating-system secret store, with no plaintext fallback.
- Optical catalogue/retrieval and PRISMA import controls; Sentinel-1 discovery,
  session-only account access, bounded downloads, SAFE inspection and shared local
  Python/SNAP processing with owned jobs, cancellation and restart recovery.
- Saved infrastructure sites, compatible acquisition pairs, shared calibrated-power
  change review, analyst decisions/drafts, map previews and original scientific exports.
- Place lookup, annotation drawing, workspace JSON and
  `helios://` view/workspace links and approximate solar terminator.
- Dated NASA daily visualization layers with bounded caches and explicit source/date
  attribution; these are distinct from dated scientific acquisitions.
- Optional local ONNX Runtime CPU assistance with explicit model contracts, retained
  source/model hashes and separately reviewed mission observation proposals.

These workflows are implemented, but Linux fixture/public-source checks do not
establish complete parity on every platform or provider. Fresh Apple Silicon Qt
Keychain/Metal checks, real SNAP/calibrated SAR pairs, keyed sources and physical
input/display/accessibility checks remain open. Visual blending and model
scores do not imply scientific registration or validated change detection.

## Build

Requirements: CMake 3.24+, a C++20 compiler and Qt 6.8+ with Core, GUI/private
headers, Widgets, Network, Concurrent, Test and Shader Tools. Qt 6.11.2 is the
tested Linux version. Earlier Mac Metal evidence was recorded for a smaller
slice; the expanded client needs a fresh Apple Silicon run. Compile against the Qt
build that you will run/deploy; RHI private interfaces do not promise
compatibility across Qt builds.

On macOS, install `qtbase` and `qtshadertools` using Homebrew, then:

```sh
make test-desktop
open output/desktop/build/bin/Helios.app
```

The build script deploys Qt frameworks/plugins into this arm64 bundle, adds the
application icon and verifies its local ad-hoc signature. The bundle is not a
notarized public release.

On Omarchy, use the normal distribution update process and then install development
dependencies, leaving the existing GPU driver unchanged:

```sh
sudo pacman -S --needed base-devel cmake qt6-base qt6-shadertools qt6-wayland vulkan-tools
./native/desktop/diagnose-linux.sh
make test-desktop
./output/desktop/build/bin/Helios
```

The Arch CI job compiles the Qt client and runs headless core tests. It does not
verify Wayland, the NVIDIA driver, Vulkan frames, physical gestures or CUDA inference.
The job is configured; its first hosted execution remains outstanding.

## Packages

After a successful Linux build, run `python3 native/desktop/package-linux.py --native`
from the repository root. This creates a relocatable archive, an Arch package,
matching source snapshot and checksums in a new `output/packages/` directory.
The archive carries the exact Qt shared libraries and selected Wayland/X11,
image and TLS plugins, plus FFmpeg and its libav/libsw libraries for native
camera clips; graphics drivers and external scientific runtimes stay
under the user's control. It targets the build host's Arch library generation.

Extract the archive and run `./helios-desktop --check-runtime`, then
`./helios-desktop`. Its optional `python3 install-user.py` installer adds a user
desktop entry without changing working profiles. See the
[packaging instructions](PACKAGING.md) for installation, removal, dependencies,
source/license records and isolated validation. CI is configured to retain Linux
artifacts and an ad-hoc signed arm64 Qt bundle; those hosted jobs still need a
first successful run and do not replace hardware acceptance.

Scientific Python, SNAP GPT and inference Python/models are explicit optional
dependencies. Set their paths in Processing Center and Local assistance. Native
camera clips use FFmpeg (bundled in the Linux artifact, external for a Mac build).
Ordinary map startup does not install these tools, models or GPU drivers.

## Working with existing data

The client uses its own application profile, or an explicit `--profile DIRECTORY`.
It never automatically imports or replaces the existing macOS mission library.

- **Mission Control → Open:** choose an existing mission JSON library. **Save as**
  writes a separate copy; **Save** deliberately updates the selected file after
  conflict checks. Concurrent changes cause a refusal rather than silent overwrite.
  Use a separate working copy for experimentation; concurrent writers are not a
  supported shared-editing protocol.
- `--mission-library /absolute/path/to/missions.json` opens a chosen library with
  automatic monitor checks paused. The Automations page can explicitly resume them.
- **Satellite → Import result.json:** choose a completed optical or SAR job manifest.
  **Add library folder** examines immediate job subfolders, capped at 500 entries.
- Choose an output and **Show selected image on map**. SAR can expose VV, VH and
  Composite separately. **Use as second comparison layer** adds a visual A/B overlay.
- Link displayed imagery to a mission; select mission evidence to focus the same map.
- Exports copy original preview/display/scientific files. Imported references can be
  removed without deleting their source data. Moving an owned completed job to OS
  Trash requires a separate action and identity/lease/hash checks; imported folders
  are not eligible for that action. Missing saved paths remain visible for retry.

Processing Center can create fresh products directly; copied Mac experiments are
not required. Saved source snapshots stay inspectable when their live provider is
unavailable. Monitors run while this app is open and do not download products or
install a background service.
The **Automations** page controls automatic checks for the session. Running a check
saves its checkpoint into the mission library; disable automatic checks when
inspecting a library without intending those updates.

Existing macOS optical products live under
`~/Library/Application Support/Helios/Satellite/optical/`; SAR products live under
`~/Library/Application Support/Helios/SAR/jobs/`. A copied mission reference alone
does not transfer its external raster files. Library paths need reimporting on Linux.

Drag to pan; right-drag or Option/Alt-drag to rotate/tilt. Scroll-wheel zoom and
trackpad pan/pinch are implemented. Wayland/NVIDIA generated and live GPU checks
passed on October 15–16, 2026. Three generated scene/two-overlay travel cycles also
passed bounded-resource/idle/hide-show checks. Physical input, other DPI/displays,
actual compositor minimization and large-site stress remain outstanding; see
[migration status](../../docs/MIGRATION-STATUS.md).

## Validation

```sh
make test-desktop
./output/desktop/build/bin/Helios.app/Contents/MacOS/Helios \
  --validate --qa-output output/qa/desktop
```

Use the Linux binary path on Linux. The native harness uses a disposable profile,
generated QA imagery and actual GPU framebuffer readback. Add `--product /path/to/result.json`
to inspect a real completed product without modifying its source files. Use
`--validate-live` instead of `--validate` for opt-in real public basemap/terrain requests.
Add `--raster-python /absolute/path/to/python3` for generated scientific-processing
and infrastructure checks. Set `HELIOS_INFERENCE_PYTHON` to an explicitly prepared
CPU inference environment for generated model/proposal checks. Fixtures are bundled
so installed-package validation does not depend on a source checkout.

Core tests cover mission validation/round trips, extension fields, conflicts,
Unicode, timestamps, refinement masks, invalid product paths/CRS, scientific versus
preview dimensions, exports and cancellation cleanup. The generated mission
round-trip fixture preserves the document format and is exported for external
comparison. The expanded suites also cover jobs/downloads,
dossier/link/pin/review/task persistence and stale-dialog isolation,
 timeline/monitor persistence, source parsers/orbits, credential boundaries, map
documents, daily caches and CPU inference admission.

See [cross-platform architecture](../../docs/CROSS-PLATFORM.md) and [notices](NOTICE.md).
Use the [Linux handoff checklist](LINUX-HANDOFF.md) when moving to the other PC.
See [sources](DOMAIN-SOURCES.md), [map tools](../../docs/MAP-TOOLS.md),
[daily visualization](../../docs/DAILY-IMAGERY.md) and
[local CPU assistance](../../docs/LOCAL-INFERENCE.md) for setup and limits.
