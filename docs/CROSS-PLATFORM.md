# Native macOS and Linux application

Updated 19 November 2026. Helios is a native C++20/Qt 6 application on Apple
Silicon macOS and Linux, with the existing feature roadmap retained. The earlier
Swift macOS client was removed; it is no longer built, tested or shipped.

## Architecture decision

- **C++20:** shared geographic, mission, evidence, job, provider and scene logic.
- **Qt 6 Widgets:** desktop windows, inspectors, satellite library and Mission Control.
  No WebEngine, Electron, HTML interface, QML or browser renderer is required.
- **GPU rendering:** Metal on macOS and Vulkan on Linux, behind Qt RHI. The renderer
  draws globe, terrain, raster overlays and shared scene meshes. Its private APIs
  require matching build/deployment Qt libraries. Historical Mac Metal and current
  Linux Vulkan checks are recorded separately in [migration status](MIGRATION-STATUS.md).
- **Scientific processing:** owned Python raster workers and SNAP GPT behind
  application-level process orchestration. Verify tools/dependencies on each OS.
- **Inference:** a replaceable provider interface with a CPU baseline and optional
  accelerated backends. CUDA is useful on compatible NVIDIA machines, but cannot
  become a dependency of the map, document library or ordinary application startup.
- **Platform integration:** C++/Qt with small Objective-C++ files for macOS Keychain
  access. There is no Swift component.

Rendering and inference are separate capabilities. A GPU that renders the map
successfully has not thereby passed model inference or scientific validation.

## Implemented workflows

`native/desktop` contains the Qt client: native map/terrain/raster rendering,
bounded scientific libraries and original exports; all 15 source layers; optical
catalogue/retrieval and local PRISMA import; Sentinel-1 search, session-only account
access, downloads and shared Python/SNAP orchestration; infrastructure sites/change
review; Mission Control timelines/monitors; map tools, workspace links and daily
visualization; and optional CPU inference with explicit model/source provenance and
analyst review. The scientific grid and bounded preview sizes remain separate.

The shared scene engine renders local and same-origin HTTPS triangle photogrammetry
in Qt, including real public AGI headquarters content on Linux. Native credentials
use optional Linux Secret Service or macOS Keychain, with no plaintext fallback;
Copernicus account sessions stay in memory. Owned process sessions support
cancellation/recovery. Offline mode rejects remote acquisition, source/media and
scene requests while retaining local inspection/processing.

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
Fresh Mac/Keychain/Metal checks, real SNAP/calibrated SAR pairs, keyed providers,
physical input/displays/accessibility and large-site stress remain external
acceptance gates. See the [migration status and evidence](MIGRATION-STATUS.md) and
[desktop build and handoff guide](../native/desktop/README.md).

## Build the shared core

On either platform, install CMake 3.24+ and a C++20 compiler, then run:

```sh
cmake -S . -B output/shared/build -DCMAKE_BUILD_TYPE=Release
cmake --build output/shared/build --parallel 2
ctest --test-dir output/shared/build --output-on-failure
```

`make test-shared` runs the same steps. The root CMake project builds the shared
library and tests. Use `make test-desktop` for the Qt client; it needs the Qt
modules documented in the desktop guide.

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
all AMD cards support a particular compute stack. The shared geometry library does
not initialize, probe or install any inference backend.

Sources checked October 13, 2026:

- [NVIDIA legacy GPU capabilities](https://developer.nvidia.com/cuda/gpus/legacy)
- [CUDA 13 release notes](https://docs.nvidia.com/cuda/archive/13.0.2/cuda-toolkit-release-notes/index.html)
- [ONNX Runtime provider architecture](https://onnxruntime.ai/docs/execution-providers/)
- [Qt RHI compatibility and graphics backends](https://doc.qt.io/qt-6/qrhi.html)

## Migration history and acceptance gates

1. **Shared core:** the C++20 geography library and its tests are in `native/shared`.
2. **Map vertical slice:** Qt window with real basemap/terrain content, Metal/Vulkan
   resource lifetime, navigation, depth, picking and georeferenced raster overlays.
3. **Data compatibility:** open existing mission documents and satellite products,
   preserve source IDs, timestamps and unknown extension data, and test round trips.
   A preview must not be mistaken for its original scientific raster.
4. **Processing and providers:** real optical retrieval, download progress, SAR
   orchestration, cancellation/recovery and appropriate platform credential storage.
5. **Complete desktop workflows:** Mission Control, notes/tasks/observations,
   linked selections, monitoring, exports and remaining layer/tool parity.
6. **Release:** real platform packages, accessibility/input checks, offline/recovery
   tests and measured CPU/GPU/memory behavior on both machines.

Stages 1–5 are represented in the Qt source and current Linux workflow tests.
Acceptance remains bounded by the evidence for each platform/provider/scientific
path. The Swift client, which previously served as the behavioral reference, was
removed after those workflows were present in Qt; its code remains available in
Git history for reference.

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
included in that transfer; cloning an older remote revision will not include this
work. Mission libraries and satellite products live outside Git in Application
Support on macOS, so copying the repository alone does not transfer those datasets.
Use explicit portable exports or a controlled copy with the current importer.
Existing Mac experiments are optional; a copied repository alone contains no
personal mission library or scientific imagery. See the [Linux handoff](../native/desktop/LINUX-HANDOFF.md)
and [package guide](../native/desktop/PACKAGING.md).
