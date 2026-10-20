# GodEye

GodEye is a native desktop workspace for geographic analysis,
satellite imagery, infrastructure review and mission planning. The map remains
the main view; floating tools and a separate Mission Control window share the
same areas, imagery and evidence.

The **macOS and Linux** C++20/Qt client now implements the GPU globe and shared
Cesium scene engine, 19 source layers, map tools, optical/SAR processing controls,
infrastructure review, Mission Control timelines/monitors and optional local CPU
model proposals. Linux builds and actual Wayland/Vulkan checks run on Omarchy
with a GTX 1070. Platform and scientific acceptance remain incomplete; see
[migration status](docs/MIGRATION-STATUS.md). The functional Swift macOS client
remains available as the reference during migration. See the
[Qt desktop guide](native/desktop/README.md) and [platform handoff](docs/CROSS-PLATFORM.md).

Both clients use compiled native rendering and networking. The Swift reference
uses SwiftUI, AppKit, Metal and Foundation; Qt uses Widgets and RHI with Vulkan on
Linux and Metal on macOS. The browser application is retired. Neither client
requires Node.js, npm, a local web server, QML or a browser runtime.

## Build and run

For the Qt client, install CMake 3.24+, a C++20 compiler and matching Qt 6.8+
Base/private headers and Shader Tools. Qt 6.11.2 is the tested Linux version.
The first build downloads pinned Cesium Native dependencies and can take several
minutes; later builds reuse them. Run the following commands from the root of
your cloned repository.

### Linux (Omarchy / Arch, x86_64)

Install the build dependencies on an up-to-date system:

```sh
sudo pacman -S --needed base-devel git cmake ninja python pkgconf curl zip unzip \
  qt6-base qt6-shadertools qt6-wayland vulkan-icd-loader vulkan-tools
./native/desktop/diagnose-linux.sh
make test-desktop
./output/desktop/build/bin/GodEyeDesktop
```

`make test-desktop` builds the Qt application and runs its regression suites.
Use `make build-desktop` to build without running tests. Launch the application
inside a graphical Wayland or X11 session with a working Vulkan driver; CUDA is
not required. The diagnostic script reports the existing environment without
installing or changing drivers.

Optional Linux dependencies are `ffmpeg` for native camera clips and `libsecret`
with a running Secret Service for saving provider keys:

```sh
sudo pacman -S --needed ffmpeg libsecret
```

To use a separate profile or inspect local data offline:

```sh
./output/desktop/build/bin/GodEyeDesktop --profile "$HOME/.local/share/GodEye-test"
./output/desktop/build/bin/GodEyeDesktop --profile "$HOME/.local/share/GodEye-test" --offline
```

If you have a Linux package archive, extract it, enter its `godeye-desktop/`
directory, and run:

```sh
./godeye-desktop --check-runtime
./godeye-desktop
```

`python3 install-user.py` in that directory optionally adds a desktop launcher.
To create archive and Arch package artifacts from your completed source build,
install `ffmpeg`, then run `python3 native/desktop/package-linux.py --native`.
See the [packaging guide](native/desktop/PACKAGING.md) for installation, removal
and runtime requirements. Build artifacts under `output/` are local and are not
included when cloning the repository.

Other Linux distributions need equivalent compiler/build tools, Vulkan support,
Qt Base including its private development headers, and Qt Shader Tools. Use the
same source-build commands with a matching Qt 6.8+ installation. Those
distributions remain unverified; the current binary packages target the Arch
system-library generation used to build them.

### macOS (Apple Silicon, Qt client)

Install the build tools with Homebrew, then build and open the separate Qt app:

```sh
brew install cmake pkgconf qtbase qtshadertools
make test-desktop
open output/desktop/build/bin/GodEyeDesktop.app
```

Fresh Mac validation of the expanded Qt client is still required.

### macOS (Swift reference client)

To build the Swift reference, use an **Apple Silicon Mac**, **macOS 14 or later**,
and full **Xcode** with the macOS SDK, Swift 6 and Metal compiler.
Install CMake 3.24+, Python 3 and pkg-config
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

### Windows

Windows builds and packages are not currently supported. See
[migration status](docs/MIGRATION-STATUS.md) for the validated platform scope.

## Workspaces

The Qt interface follows the macOS workspace with floating map palettes, a
consistent dark analyst theme and a Mission Control sidebar. Search is available
with **Ctrl+K** on Linux; **Ctrl+Shift+H** hides the palettes. See the
[desktop design and navigation guide](docs/DESKTOP-DESIGN.md) for panel controls,
keyboard access and the relationship to the Swift reference.

- **Map:** geographic search, terrain and 3D Tiles, layers, record inspection,
  raw data, annotations, directions and saved view state.
- **Satellite:** a searchable saved library, dated optical acquisitions,
  Sentinel-1 GRD search/download/processing and infrastructure change review.
  Sentinel-2 is the default optical source. CBERS-4A and imported PRISMA products
  have their own coverage, availability and resolution limits.
- **Marine, Sky and Ground:** grouped public sources including vessels, cables,
  aircraft, propagated satellite positions, infrastructure and environmental data.
- **Mission Control**: a separate window for mission areas, linked evidence,
  timelines, reviewed observations, notes, tasks and acquisition checks. Its
  actions update the shared map. **Areas** connects each site’s evidence, reference
  acquisitions, reviews, notes and follow-up tasks in a [site dossier](docs/SITE-DOSSIERS.md).
  **Site monitoring** adds stable controls, verified relative translation, local SAR
  analysis and a persistent review inbox. See the [monitoring guide](docs/SITE-MONITORING.md).
  Automations run while GodEye is open.

Provider data can be delayed, incomplete or unavailable. Propagated orbits are
estimates; basemap mosaics do not establish an acquisition date. Raster blending
is visual comparison. Measured SAR change review is not a validated construction
or damage detector. See the workflow guides for scientific and operational limits.

## Satellite processing setup

The map and most public sources work without SAR tools or credentials. To process
radar or retrieve optical raster pixels, configure a separate scientific Python.

### Qt client on Linux or macOS

Use an installed Python 3.12 interpreter to create an isolated environment:

```sh
/absolute/path/to/python3.12 -m venv .venv
.venv/bin/python -m pip install \
  -r native/macos/Sources/GodEyeMac/Resources/SAR/requirements.txt
```

In **Processing Center → Engine and jobs**, set **Raster Python** to the environment's
Python executable (the absolute path to `.venv/bin/python`), then click **Check Python
raster imports**. This selection is saved for that profile. The default system
Python may not contain these packages. On Linux you can also select it at launch:

```sh
./output/desktop/build/bin/GodEyeDesktop --raster-python "$PWD/.venv/bin/python"
```

The workers remain under `native/macos/` in the source tree and are shared by both
platforms; the Qt build copies them into its runtime resources. The environment
provides NumPy, Rasterio/GDAL, Pillow and h5py. Scientific Python and SNAP are
external dependencies even when using a packaged application.

### Swift reference on macOS

```sh
make setup-raster
```

Select its Python executable in the satellite processing settings.

### Radar processing and local models

Sentinel-1 calibration and terrain correction also require **ESA SNAP 14+ with the Microwave
Toolbox**; install it separately and select its **`bin/gpt`** executable. SNAP's
Python bridge and GUI do not need to be running. Orbit/DEM downloads can require
network access. Processing preserves floating-point scientific products; previews
are bounded display images.

Native camera clips additionally use an explicit FFmpeg executable. Optional
[CPU inference](docs/LOCAL-INFERENCE.md) has its own pinned Python requirements
and model contract; it never downloads weights automatically.

## Development and tests

On Linux and macOS, the portable Qt/core checks are:

```sh
make test-shared    # standalone C++ geography tests
make test-desktop   # build Qt desktop client and run native regression suites
```

With the scientific environment above prepared, run the shared raster fixtures
on either platform:

```sh
GODEYE_SAR_PYTHON="$PWD/.venv/bin/python" make test-raster
```

The Swift/Xcode commands below are macOS-only:

```sh
make test          # Swift/core/scene tests; builds the native engine if needed
make format-check  # Swift formatting using Xcode's swift-format
make check         # formatting, shared core, Swift and raster checks
```

`make build`, `make run`, `make setup-raster` and `make check` use the macOS
reference tooling. Linux users should use the desktop targets and explicit raster
environment above.

`make format` formats native Swift sources. Individual scripts remain available
under `native/macos/`. Opt-in public-provider checks use
`GODEYE_LIVE_CHECKS=1 native/macos/test.sh --filter Live`. Real SNAP processing,
independent image registration/radiometric validation, aerial detector accuracy,
optional GPU inference and complete manual UI coverage remain separate acceptance
checks. A trained CPU model execution is recorded in migration status.

## Project layout

- `native/macos/Sources/GodEyeMac/`: SwiftUI/AppKit workspace, Metal renderer and
  optional local satellite workers.
- `native/macos/Sources/GodEyeCore/`: validated models, data sources, storage,
  processing orchestration and canonical bundled reference datasets.
- `native/scene/`: shared C++ scene adapter and pinned Cesium Native build;
  `native/macos/Sources/CScene/` retains the Swift bridge/transport integration.
- `native/macos/Sources/CSGP4/`: native Vallado orbital propagation.
- `native/shared/`: portable C++20 geography with CMake and SwiftPM integration.
- `native/desktop/`: C++/Qt client, GPU map, source/workflow/storage modules and tests.
- `native/macos/Tests/`: Swift fixtures and scientific raster tests.
- `native/macos/Validation/`: opt-in satellite validation tools.
- `docs/`: maintained native workflow and architecture guides.

## Guides

- [API model handoff: complete the macOS/Linux migration](MIGRATION-HANDOFF.md)
- [Product roadmap and next implementation milestones](docs/ROADMAP.md)
- [Cross-platform architecture and Linux handoff](docs/CROSS-PLATFORM.md)
- [Architecture and native rendering](docs/NATIVE-MACOS.md)
- [Current capabilities and limits](docs/CURRENT-STATE.md)
- [Mission Control](docs/MISSION-CONTROL.md)
- [Connected site dossiers in the shared Qt client](docs/SITE-DOSSIERS.md)
- [Site monitoring and verified relative correction](docs/SITE-MONITORING.md)
- [Map workspace and evidence overlays](docs/MAP-WORKSPACE-AND-MISSIONS.md)
- [Map navigation performance and benchmark](docs/MAP-PERFORMANCE.md)
- [Sentinel-1 SAR](docs/SENTINEL1-SAR.md)
- [Sentinel-2 optical imagery](docs/SENTINEL2-OPTICAL.md)
- [Optical sources: Sentinel-2, CBERS-4A and PRISMA](docs/OPTICAL-SATELLITE-SOURCES.md)
- [Infrastructure change review](docs/SATELLITE-INFRASTRUCTURE-REVIEW.md)
- [SAR quality and evaluation](docs/SAR-QUALITY-AND-MONITORING.md)
- [Local object detection](docs/LOCAL-OBJECT-DETECTION.md)
- [Qt local CPU assistance and model contracts](docs/LOCAL-INFERENCE.md)
- [Qt source layers and native camera playback](native/desktop/DOMAIN-SOURCES.md)
- [Map tools, annotations and workspace links](docs/MAP-TOOLS.md)
- [Daily visualization sources](docs/DAILY-IMAGERY.md)
- [Satellite analysis roadmap](docs/SATELLITE-ANALYSIS-PLAN.md)
- [Testing](TESTING.md), [contributing](CONTRIBUTING.md), [security](SECURITY.md)

## Storage and licensing

Mission documents, annotations, satellite downloads and processed images live
under `~/Library/Application Support/GodEye/` for the Swift app. Qt uses its own
platform application-data directory, or an explicit `--profile DIRECTORY`; it
does not automatically replace or import the Swift library. Provider credentials
use macOS Keychain or optional Linux Secret Service. Copernicus account sessions
remain in memory. Removing source/build files does not remove a working profile.

The source license is [MIT](LICENSE); existing copyright notices remain intact.
Bundled datasets, map providers, native components and optional detector models
have separate terms. In particular, the bundled TeleGeography cable data is
**CC BY-NC-SA 3.0** and is not licensed for commercial use. See
[data sources and attribution](DATA_SOURCES.md), [notices](NOTICE.md), and the
resource-specific READMEs before redistribution.

## Connected investigations

Use Mission Control’s Investigation, Asset registry, Connections and Findings pages to retain shared infrastructure identities, review dated evidence, track dependent follow-up and export an A4 PDF with a JSON evidence manifest. See [Connected investigations](docs/CONNECTED-INVESTIGATIONS.md) for the complete workflow and historical-record rules.
