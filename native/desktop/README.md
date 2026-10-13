# GodEye C++ / Qt desktop preview

This is the first functional desktop slice for macOS and Linux. It is maintained
alongside the full Swift macOS client while native workflows are migrated.

## Implemented

- Qt Widgets map workspace and a separate Mission Control window sharing one model.
- GPU rendering through Qt RHI: Metal on the tested Mac, Vulkan selected on Linux.
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

The client does not yet provide photorealistic 3D Tiles, live domain feeds,
satellite catalog/download/processing controls, acquisition monitors or model
inference. Those remain available in the existing macOS client where implemented.
Visual overlays do not imply scientific registration or validated change detection.

## Build

Requirements: CMake 3.24+, a C++20 compiler and Qt 6.8+ with Core, GUI/private
headers, Widgets, Network, Concurrent, Test and Shader Tools. Qt 6.11.2 is the
locally tested version. Compile against the Qt build that you will run/deploy;
RHI private interfaces do not promise compatibility across Qt builds.

On macOS, install `qtbase` and `qtshadertools` using Homebrew, then:

```sh
make test-desktop
open output/desktop/build/bin/GodEyeDesktop.app
```

The build script deploys Qt frameworks/plugins into this separate arm64 preview
bundle and verifies its local ad-hoc signature. It does not replace
`output/native/GodEye.app`. Neither bundle is a notarized public release.

On Omarchy, use the normal distribution update process and then install development
dependencies, leaving the existing GPU driver unchanged:

```sh
sudo pacman -S --needed base-devel cmake qt6-base qt6-shadertools qt6-wayland vulkan-tools
./native/desktop/diagnose-linux.sh
make test-desktop
./output/desktop/build/bin/GodEyeDesktop
```

The Arch CI job compiles the Qt client and runs headless core tests. It does not
verify Wayland, the NVIDIA driver, Vulkan frames, physical gestures or CUDA inference.
The job is configured; its first hosted execution remains outstanding.

## Working with existing data

The preview uses its own application profile, or an explicit `--profile DIRECTORY`.
It never automatically imports or replaces the existing macOS mission library.

- **Mission Control → Open:** choose an existing mission JSON library. **Save as**
  writes a separate copy; **Save** deliberately updates the selected file after
  conflict checks. Concurrent changes cause a refusal rather than silent overwrite.
  Use separate working copies while both clients are open: the old Swift writer
  does not participate in the Qt file lock, so this is not a shared-editing protocol.
- **Satellite → Import result.json:** choose a completed optical or SAR job manifest.
  **Add library folder** examines immediate job subfolders, capped at 500 entries.
- Choose an output and **Show selected image on map**. SAR can expose VV, VH and
  Composite separately. **Use as second comparison layer** adds a visual A/B overlay.
- Link displayed imagery to a mission; select mission evidence to focus the same map.
- Exports copy original preview/display/scientific files. Imported references can be
  removed without deleting their source data.

Existing macOS optical products live under
`~/Library/Application Support/GodEye/Satellite/optical/`; SAR products live under
`~/Library/Application Support/GodEye/SAR/jobs/`. A copied mission reference alone
does not transfer its external raster files. Library paths need reimporting on Linux.

Drag to pan; right-drag or Option/Alt-drag to rotate/tilt. Scroll-wheel zoom and
trackpad pan/pinch are implemented. Real Wayland/NVIDIA and physical input checks
remain the next hardware acceptance gate.

## Validation

```sh
make test-desktop
./output/desktop/build/bin/GodEyeDesktop.app/Contents/MacOS/GodEyeDesktop \
  --validate --qa-output output/qa/desktop
```

Use the Linux binary path on Linux. The native harness uses a disposable profile,
generated QA imagery and actual GPU framebuffer readback. Add `--product /path/to/result.json`
to inspect a real completed product without modifying its source files. Use
`--validate-live` instead of `--validate` for opt-in real public basemap/terrain requests.

Core tests cover mission validation/round trips, extension fields, conflicts,
Unicode, timestamps, refinement masks, invalid product paths/CRS, scientific versus
preview dimensions, exports and cancellation cleanup. The Qt-authored generated
mission fixture is also read and rewritten by the Swift tests.

See [cross-platform architecture](../../docs/CROSS-PLATFORM.md) and [notices](NOTICE.md).
Use the [Linux handoff checklist](LINUX-HANDOFF.md) when moving to the other PC.
