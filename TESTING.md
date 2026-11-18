# Testing Helios

Run these commands from the repository root on an Apple Silicon Mac with the
native build prerequisites from [README.md](README.md).

```sh
make format-check
make test-shared
make test
make test-raster
make build
```

`make test` builds the pinned native scene engine, then runs Swift tests for
geography, source admission, resource catalogs, orbital propagation, scenes,
mission documents, timelines, satellite products and processing orchestration.
Ordinary tests use bounded local fixtures. Public-provider checks are opt-in:

```sh
HELIOS_LIVE_CHECKS=1 native/macos/test.sh --filter Live
```

`make test-raster` uses the Python environment created by `make setup-raster`.
Select another environment explicitly with
`HELIOS_SAR_PYTHON=/absolute/path/to/python3 make test-raster`.
Tests cover SAFE/ZIP admission, noise/orbit graph configuration, calibration
pipeline boundaries, dB conversion, nodata, display/composite scaling, georeferenced
optical raster reads, bounded requests, retries, cancellation and failure cleanup.
Generated fixtures are not operational satellite acquisitions.

## Native window and map checks

The app's opt-in harness uses a disposable library and never needs real user
missions. Its root name must start with `helios-mission-validation-`:

```sh
helios_validation_root="$(mktemp -d /tmp/helios-mission-validation-ui.XXXXXX)"
HELIOS_VALIDATION_ROOT="$helios_validation_root" \
HELIOS_VALIDATION_FIXTURES="$PWD/native/macos/Tests/HeliosCoreTests/Fixtures" \
output/native/Helios.app/Contents/MacOS/HeliosMac --mission-validation
```

Read `validation.json` and inspect the screenshots in that root. The harness
checks native window/model integration, shared map actions, mission persistence,
evidence links, notes, observations, tasks, monitor checkpoints, exports, panel
focus and light/dark compact layouts. Screenshots require an unlocked graphical
session and screen-recording access. This is programmatic native state validation;
manual pointer checks remain necessary for dragging, resizing and menu behavior.
Remove only the disposable root you created after retaining needed evidence.

`output/native/Helios.app/Contents/MacOS/HeliosMac --native-smoke` performs a short
native map/provider smoke check and terminates. It uses the normal application
profile; use the isolated mission harness when modifying test mission state.

## Scientific acceptance

Unit tests and generated rasters do not establish geographic/radiometric accuracy
or detector performance. Use real, identified products and independent controls.
Retain processing versions/options, compare scientific rasters with independent
references, measure held-out registration residuals, and evaluate change proposals
against real dated events. See [SAR validation](docs/SAR-QUALITY-AND-MONITORING.md)
and the [satellite plan](docs/SATELLITE-ANALYSIS-PLAN.md).

## Portable shared core

`make test-shared` builds the standalone C++20 geography module with CMake/CTest.
It needs a C/C++ compiler and CMake 3.24+, without Swift, Qt, Apple frameworks or
CUDA. The tests exercise the C ABI, WGS84 reference positions and round trips,
ray distances, camera geometry, invalid inputs and output preservation on failure.
Tests use explicit checks that remain enabled in release builds.

The Swift scene/geography tests also exercise this same C++ implementation through
the application's adapters, including reverse-depth projection and surface picking.
The shared-core Linux CI job does not establish Qt, Wayland, Vulkan, NVIDIA driver
or inference compatibility. The separate Qt client now exists; its real Linux
window, renderer and input checks still require a hardware session.

## Qt desktop preview

`make test-desktop` builds the separate C++/Qt preview, deploys/signs its macOS
bundle when applicable, and runs CMake/CTest core checks. The Arch container CI
job compiles this client and runs headless tests. GPU/window validation is a
separate opt-in native harness, documented in [the desktop guide](native/desktop/README.md).
Generated imagery tests verify orientation, blending, draped geometry, picking,
window resize and resource removal. Live checks explicitly request real provider
tiles or a selected existing product. Neither test establishes SAR scientific accuracy.

GitHub Actions is configured for the shared core on Linux and native checks on an
Apple Silicon macOS runner. No browser,
Node dependency installation or website test suite is part of the project.

## Connected investigation regressions

The desktop suite includes `desktop-investigations` and
`desktop-investigations-ui`. They cover legacy/portable documents, registry
conflicts, snapshot preservation, missing evidence, archive/import behavior,
review resets, source promotion, task cycles, stale dialogs, frozen briefing
previews and bounded graphs. Run them through the existing `make test-desktop`
workflow or select these targets with CTest.

Set `HELIOS_INVESTIGATION_QA` to an absolute output directory when running
`HeliosInvestigationTests` or `HeliosInvestigationUiTests` directly to retain the
civilian evaluation library, briefings, screenshots and graph timing evidence.
UI/headless runs use `QT_QPA_PLATFORM=offscreen`. These generated fixtures are not
real satellite-analysis validation. `HeliosDesktop --validate` additionally
exercises the connected workflow with the native GPU and a captured map.

Swift fixture/registry regressions are in
`native/macos/Tests/HeliosCoreTests/InvestigationTests.swift`. A fresh Mac execution
and real cross-platform case round-trip remain required acceptance gates.
