# Native macOS and Linux transition

Updated October 13, 2026. The product direction is now a native application on both
Apple Silicon macOS and Linux, with the existing feature roadmap retained.

## Architecture decision

- **C++20:** shared geographic, mission, evidence, job and provider logic.
- **Qt 6 Widgets:** desktop windows, inspectors, satellite library and Mission Control.
  No WebEngine, Electron, HTML interface or browser renderer is required.
- **GPU rendering:** Metal on macOS and Vulkan on Linux, behind a rendering
  boundary. The first map/terrain/raster slice now uses Qt RHI and runs on Metal.
  Its private rendering APIs require a pinned/tested Qt minor version.
- **Scientific processing:** retain Python raster workers and SNAP GPT behind
  application-level process orchestration. Verify tools/dependencies on each OS.
- **Inference:** a replaceable provider interface with a CPU baseline and optional
  accelerated backends. CUDA is useful on compatible NVIDIA machines, but cannot
  become a dependency of the map, document library or ordinary application startup.

Rendering and inference are separate capabilities. A GPU that renders the map
successfully has not thereby passed model inference or scientific validation.

## Implemented first step

`native/shared` is a standard C++20 library, built from identical sources by CMake
and SwiftPM. The current macOS app uses it for:

- WGS84 geographic/ECEF conversion and rendered surface coordinates;
- double-precision camera bases and screen picking rays;
- ellipsoid intersections and triangle hit distances.

The C ABI contains plain numeric structs and explicit success/failure results,
with no Apple, Qt or GPU dependency. CMake tests run independently of the macOS
scene engine. Existing Swift scene/geography tests exercise the adapter. The
current renderer, file formats and mission storage remain in the macOS client.

The next slice is now implemented in `native/desktop`: Qt windows, GPU globe and
terrain, public imagery loading, bounded optical/SAR preview import/overlays,
portable mission editing and original-file exports. Actual Metal frames, real
basemap/terrain requests and saved products have been checked on the Mac. The
scientific grid and bounded preview sizes remain separate.

Photorealistic 3D Tiles, remaining provider/processing controls, acquisition
automation, inference, credentials and full desktop parity still need migration.
The build targets Linux, but Linux compilation and real Omarchy/GTX 1070
validation remain outstanding before treating that platform as supported. See the
[desktop build and handoff guide](../native/desktop/README.md).

## Build the shared core

On either platform, install CMake 3.24+ and a C++20 compiler, then run:

```sh
cmake -S . -B output/shared/build -DCMAKE_BUILD_TYPE=Release
cmake --build output/shared/build --parallel 2
ctest --test-dir output/shared/build --output-on-failure
```

`make test-shared` runs the same steps. The root CMake project currently builds
the shared library and tests. Use `make test-desktop` for the Qt preview; it needs
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

Do not remove the working macOS client or claim Linux desktop parity based on a
successful shared-library build. The same behavioral tests and real reference
products should follow each migrated workflow.

## Linux handoff

The Mac map/mission/product slice is now at the Linux hardware handoff gate.
The PC is needed to verify real Wayland/window/input and NVIDIA/Vulkan behavior
before extending the renderer and completing feature migration. CUDA inference
remains a separate later check. Linux CI can catch compiler and unit-test issues;
it cannot substitute for that hardware session.

When ready, transfer the updated repository revision, install the documented
dependencies and run the shared checks first. Local uncommitted work must be
included in that transfer; cloning an older remote revision will not include it.
Mission libraries and satellite products live outside Git in Application Support
on macOS, so copying the repository alone does not transfer those datasets. Use
explicit portable exports or a controlled copy when the Linux importer is ready.
