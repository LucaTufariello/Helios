# GodEye

GodEye is a native Apple Silicon macOS workspace for geographic analysis,
satellite imagery, infrastructure review and mission planning. The map remains
the main view; floating tools and a separate Mission Control window share the
same areas, imagery and evidence.

The application is beginning a native **macOS and Linux** transition using a
shared C++20 core and Qt 6 desktop interface. A separate Qt preview now provides
GPU map/terrain rendering, existing satellite-product viewing and mission workflows.
It has been tested on this Mac; Linux hardware acceptance is the next gate. The
full Swift client remains available during migration. See the
[desktop preview](native/desktop/README.md) and [platform handoff](docs/CROSS-PLATFORM.md).

The application uses SwiftUI, AppKit, Metal, Foundation networking and a compiled
Cesium Native engine. The browser application has been retired. Build and test
workflows require no Node.js, npm, Vite, local web server or browser runtime.

## Build and run

Use an **Apple Silicon Mac**, **macOS 14 or later**, and full **Xcode** with the
macOS SDK, Swift 6 and Metal compiler. Install CMake 3.24+, Python 3 and pkg-config
(Homebrew packages `cmake` and `pkgconf`). A first engine build downloads pinned
native dependencies and can take several minutes; subsequent builds reuse them.

From the repository root:

```sh
brew install cmake pkgconf
make build
open output/native/GodEye.app
```

The build produces an arm64 app and verifies a local ad-hoc signature. It is not
a Developer ID signed or notarized release. Validation on a separate macOS 14
machine remains outstanding. `make run` builds and opens the same application.

## Workspaces

- **Map:** geographic search, terrain and 3D Tiles, layers, record inspection,
  raw data, annotations, directions and saved view state.
- **Satellite:** a searchable saved library, dated optical acquisitions,
  Sentinel-1 GRD search/download/processing and infrastructure change review.
  Sentinel-2 is the default optical source. CBERS-4A and imported PRISMA products
  have their own coverage, availability and resolution limits.
- **Marine, Sky and Ground:** grouped public sources including vessels, cables,
  aircraft, propagated satellite positions, infrastructure and environmental data.
- **Mission Control** (`⌘6`): a separate window for mission areas, linked evidence,
  timelines, reviewed observations, notes, tasks and acquisition checks. Its
  actions update the shared map. Automations run while GodEye is open.

Provider data can be delayed, incomplete or unavailable. Propagated orbits are
estimates; basemap mosaics do not establish an acquisition date. Raster blending
is visual comparison. Measured SAR change review is not a validated construction
or damage detector. See the workflow guides for scientific and operational limits.

## Satellite processing setup

The map and most public sources work without SAR tools or credentials. To process
radar or retrieve optical raster pixels, prepare an arm64 Python environment:

```sh
make setup-raster
```

Select its Python executable in the satellite processing settings. Sentinel-1
calibration and terrain correction also require **ESA SNAP 14+ with the Microwave
Toolbox**; install it separately and select its **`bin/gpt`** executable. SNAP's
Python bridge and GUI do not need to be running. Orbit/DEM downloads can require
network access. Processing preserves floating-point scientific products; previews
are bounded display images.

## Development and tests

```sh
make test           # Swift/core/scene tests; builds the native engine if needed
make test-shared    # standalone C++ geography tests; macOS or Linux
make test-desktop   # build Qt desktop preview and run its core tests
make test-raster    # SAR and optical Python tests; requires the raster environment
make format-check  # Swift formatting using Xcode's swift-format
make check          # formatting, shared core, Swift and raster checks
```

`make format` formats native Swift sources. Individual scripts remain available
under `native/macos/`. Opt-in public-provider checks use
`GODEYE_LIVE_CHECKS=1 native/macos/test.sh --filter Live`. Real SNAP processing,
independent image registration/radiometric validation, trained-model inference
and complete manual UI coverage are separate acceptance checks.

## Project layout

- `native/macos/Sources/GodEyeMac/`: SwiftUI/AppKit workspace, Metal renderer and
  optional local satellite workers.
- `native/macos/Sources/GodEyeCore/`: validated models, data sources, storage,
  processing orchestration and canonical bundled reference datasets.
- `native/macos/Sources/CScene/` and `native/macos/engine/`: Objective-C++ scene
  bridge and pinned Cesium Native build.
- `native/macos/Sources/CSGP4/`: native Vallado orbital propagation.
- `native/shared/`: portable C++20 geography with CMake and SwiftPM integration.
- `native/desktop/`: C++/Qt client, GPU map, portable mission/product readers and tests.
- `native/macos/Tests/`: Swift fixtures and scientific raster tests.
- `native/macos/Validation/`: opt-in satellite validation tools.
- `docs/`: maintained native workflow and architecture guides.

## Guides

- [Product roadmap and next implementation milestones](docs/ROADMAP.md)
- [Cross-platform architecture and Linux handoff](docs/CROSS-PLATFORM.md)
- [Architecture and native rendering](docs/NATIVE-MACOS.md)
- [Current capabilities and limits](docs/CURRENT-STATE.md)
- [Mission Control](docs/MISSION-CONTROL.md)
- [Map workspace and evidence overlays](docs/MAP-WORKSPACE-AND-MISSIONS.md)
- [Sentinel-1 SAR](docs/SENTINEL1-SAR.md)
- [Sentinel-2 optical imagery](docs/SENTINEL2-OPTICAL.md)
- [Optical sources: Sentinel-2, CBERS-4A and PRISMA](docs/OPTICAL-SATELLITE-SOURCES.md)
- [Infrastructure change review](docs/SATELLITE-INFRASTRUCTURE-REVIEW.md)
- [SAR quality and evaluation](docs/SAR-QUALITY-AND-MONITORING.md)
- [Local object detection](docs/LOCAL-OBJECT-DETECTION.md)
- [Satellite analysis roadmap](docs/SATELLITE-ANALYSIS-PLAN.md)
- [Testing](TESTING.md), [contributing](CONTRIBUTING.md), [security](SECURITY.md)

## Storage and licensing

Mission documents, annotations, satellite downloads and processed images live
under `~/Library/Application Support/GodEye/`. Provider credentials use macOS
Keychain. Removing repository source/build files does not remove this library.

The source license is [MIT](LICENSE); existing copyright notices remain intact.
Bundled datasets, map providers, native components and optional detector models
have separate terms. In particular, the bundled TeleGeography cable data is
**CC BY-NC-SA 3.0** and is not licensed for commercial use. See
[data sources and attribution](DATA_SOURCES.md), [notices](NOTICE.md), and the
resource-specific READMEs before redistribution.
