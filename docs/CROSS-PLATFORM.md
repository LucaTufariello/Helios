# Native macOS and Linux transition

Updated October 16, 2026. The product direction is a native application on both
Apple Silicon macOS and Linux, with the existing feature roadmap retained.

## Architecture decision

- **C++20:** shared geographic, mission, evidence, job and provider logic.
- **Qt 6 Widgets:** desktop windows, inspectors, satellite library and Mission Control.
  No WebEngine, Electron, HTML interface, QML or browser renderer is required.
- **GPU rendering:** Metal on macOS and Vulkan on Linux, behind a rendering
  boundary. Qt RHI renders globe, terrain, raster overlays and shared scene meshes.
  Its private APIs require matching build/deployment Qt libraries. Historical Mac
  Metal and current Linux Vulkan checks are recorded separately.
- **Scientific processing:** retain Python raster workers and SNAP GPT behind
  application-level process orchestration. Verify tools/dependencies on each OS.
- **Inference:** a replaceable provider interface with a CPU baseline and optional
  accelerated backends. CUDA is useful on compatible NVIDIA machines, but cannot
  become a dependency of the map, document library or ordinary application startup.

Rendering and inference are separate capabilities. A GPU that renders the map
successfully has not thereby passed model inference or scientific validation.

## Implemented migration workflows

`native/shared` is a standard C++20 library, built from identical sources by CMake
and SwiftPM. The current macOS app uses it for:

- WGS84 geographic/ECEF conversion and rendered surface coordinates;
- double-precision camera bases and screen picking rays;
- ellipsoid intersections and triangle hit distances.

The C ABI contains plain numeric structs and explicit success/failure results,
with no Apple, Qt or GPU dependency. CMake tests run independently of the macOS
scene engine. Existing Swift scene/geography tests exercise the adapter. The
Swift renderer and storage remain functional while Qt implements compatible
document/product readers and preserves extension data and scientific originals.

`native/desktop` now contains the expanded Qt client: native map/terrain/raster
rendering, bounded scientific libraries and original exports; all 19 source
layers; optical catalogue/retrieval and local PRISMA import; Sentinel-1 search,
session-only account access, downloads and shared Python/SNAP orchestration;
infrastructure sites/change review; Mission Control timelines/monitors; map tools,
workspace links and daily visualization; and optional CPU inference with explicit
model/source provenance and analyst review. The scientific grid and bounded
preview sizes remain separate.

The shared scene engine now also renders local and same-origin HTTPS triangle
photogrammetry in Qt, including real public AGI headquarters content on Linux.
Native credentials use optional Linux Secret Service or macOS Keychain, with no
plaintext fallback; Copernicus account sessions stay in memory. Owned process
sessions support cancellation/recovery. Offline mode rejects remote acquisition,
source/media and scene requests while retaining local inspection/processing.

The client compiles on Omarchy and passes generated workflow, public-provider and
actual Vulkan checks on the GTX 1070. Three generated scene/two-overlay travel
cycles passed bounded-resource, idle and hide/show checks; actual compositor
minimize/restore remains unverified. Linux archive/native-package machinery,
outside-checkout startup and native FFmpeg playback have been exercised. A native
status-callback reentrancy crash was corrected and the updated client passed 104
workflow checks plus 76 resource checks; three compositor-minimize checks remain
explicitly skipped. The final R3 Linux package passes outside-checkout runtime,
installer, 104 generated native, 19 live GPU, 25 actual optical and native FFmpeg
media checks; source/runtime hashes and superseded failures are retained.
Fresh Mac/Swift/Keychain/Metal checks, real SNAP/calibrated SAR pairs,
keyed providers, physical input/displays/accessibility and large-site stress
remain external acceptance gates. See the
[migration status and evidence](MIGRATION-STATUS.md) and
[desktop build and handoff guide](../native/desktop/README.md).

## Build the shared core

On either platform, install CMake 3.24+ and a C++20 compiler, then run:

```sh
cmake -S . -B output/shared/build -DCMAKE_BUILD_TYPE=Release
cmake --build output/shared/build --parallel 2
ctest --test-dir output/shared/build --output-on-failure
```

`make test-shared` runs the same steps. The root CMake project currently builds
the shared library and tests. Use `make test-desktop` for the Qt client; it needs
the Qt modules documented in the desktop guide. Building the existing full app
still uses `make build` on macOS.

## GPU policy and the first Linux machine

The first Linux hardware target is Omarchy with a **GeForce GTX 1070**. NVIDIA
lists this card at compute capability **6.1** (Pascal). CUDA 13 removed offline
compilation and library support for Pascal, so an inference backend for this card
must use a tested CUDA 12.x-compatible dependency set that actually includes
Pascal kernels. Installing the newest CUDA package does not establish support.

Do not install or change GPU drivers automatically. Inspect the existing driver,
Vulkan support and compatible runtime versions on that PC before choosing the
inference package. CPU fallback remains available when GPU inference is unsupported;
the UI must disclose the actual backend and fallback reason.

AMD users should have the same application and map through Vulkan. Optional AMD
inference acceleration requires a separately tested supported backend/card/driver
combination; CPU execution is the common baseline. The architecture must not assume
all AMD cards support a particular compute stack. The first shared geometry library
does not initialize, probe or install any inference backend.

Sources checked October 13, 2026:

- [NVIDIA legacy GPU capabilities](https://developer.nvidia.com/cuda/gpus/legacy)
- [CUDA 13 release notes](https://docs.nvidia.com/cuda/archive/13.0.2/cuda-toolkit-release-notes/index.html)
- [ONNX Runtime provider architecture](https://onnxruntime.ai/docs/execution-providers/)
- [Qt RHI compatibility and graphics backends](https://doc.qt.io/qt-6/qrhi.html)

## Migration order and acceptance gates

1. **Shared core:** extract behavior with existing regression coverage; maintain
   the Swift adapter until the Qt client replaces it. Add Linux CMake CI.
2. **Map vertical slice:** Qt window with real basemap/terrain content, Metal/Vulkan
   resource lifetime, navigation, depth, picking and georeferenced raster overlays.
   Validate on this Mac and the actual Omarchy/GTX 1070 machine.
3. **Data compatibility:** open existing mission documents and satellite products,
   preserve source IDs, timestamps and unknown extension data, and test round trips.
   A preview must not be mistaken for its original scientific raster.
4. **Processing and providers:** real optical retrieval, download progress, SAR
   orchestration, cancellation/recovery and appropriate platform credential storage.
5. **Complete desktop workflows:** Mission Control, notes/tasks/observations,
   linked selections, monitoring, exports and remaining layer/tool parity.
6. **Release:** real platform packages, accessibility/input checks, offline/recovery
   tests and measured CPU/GPU/memory behavior on both machines.

The implementation stages above are represented in the Qt source and current
Linux workflow tests. Their acceptance remains bounded by the evidence for each
platform/provider/scientific path. Keep the working Swift macOS reference until
fresh Mac and remaining real-data/hardware gates pass.

## Linux handoff

The October 15–16 sessions established compiled workflows, core/raster/sanitizer
checks and actual Wayland/Vulkan frames on the GTX 1070. Retained evidence includes
an identified public Sentinel-2 acquisition, public scene/terrain/daily data,
keyless sources, place lookup/routing and generated processing/mission/model
review. Reports, failures and inspected images are indexed in
[migration status](MIGRATION-STATUS.md). CPU ONNX execution is the offered
inference baseline; optional CUDA/AMD acceleration remains separate from map
rendering. Configured Linux/arm64 Mac CI cannot replace hardware sessions and has
not yet been claimed to run remotely.

When ready, transfer the updated repository revision, install the documented
dependencies and run the shared checks first. Local uncommitted work must be
included in that transfer; cloning an older remote revision will not include it.
Mission libraries and satellite products live outside Git in Application Support
on macOS, so copying the repository alone does not transfer those datasets. Use
explicit portable exports or a controlled copy with the current importer. Existing
Mac experiments are optional; a copied repository alone contains no personal
mission library or scientific imagery. See the [Linux handoff](../native/desktop/LINUX-HANDOFF.md)
and [package guide](../native/desktop/PACKAGING.md).
