# Testing Helios

Run these commands from the repository root on an Apple Silicon Mac or Linux with
the build prerequisites from [README.md](README.md).

```sh
make format-check
make test-shared
make test-desktop
make test-raster
```

`make test-shared` builds the standalone C++20 geography module with CMake/CTest.
It needs a C/C++ compiler and CMake 3.24+, without Qt, Apple frameworks or CUDA.
The tests exercise the C ABI, WGS84 reference positions and round trips, ray
distances, camera geometry, invalid inputs and output preservation on failure.
Tests use explicit checks that remain enabled in release builds.

`make test-desktop` builds the Qt client (including the pinned Cesium Native
scene engine on the first build), deploys/signs its macOS bundle when applicable,
and runs the portable CTest suites. Ordinary tests use bounded local fixtures.
GPU/window validation is a separate opt-in harness, documented in
[the desktop guide](native/desktop/README.md) and [migration status](docs/MIGRATION-STATUS.md).
Generated imagery tests verify orientation, blending, draped geometry, picking,
window resize and resource removal. Live checks explicitly request real provider
tiles or a selected existing product. Neither test establishes SAR scientific accuracy.

`make test-raster` runs the owned Python worker tests in `native/workers/tests/`.
Create an environment first with `make setup-raster`, or select one explicitly:

```sh
HELIOS_SAR_PYTHON=/absolute/path/to/python3 make test-raster
```

Tests cover SAFE/ZIP admission, noise/orbit graph configuration, calibration
pipeline boundaries, dB conversion, nodata, display/composite scaling, georeferenced
optical raster reads, bounded requests, retries, cancellation and failure cleanup.
Generated fixtures are not operational satellite acquisitions.

## Qt window and map checks

The Qt harness uses a disposable profile and generated QA data; it never needs
real user missions:

```sh
./output/desktop/build/bin/HeliosDesktop.app/Contents/MacOS/HeliosDesktop \
  --validate --qa-output output/qa/desktop
```

On Linux use `./output/desktop/build/bin/HeliosDesktop`. Inspect
`validation.json`, the generated imagery and the briefing output. Add
`--product /absolute/path/to/copied-job/result.json` to inspect a copied completed
product, or `--validate-live` to request real public basemap/terrain data.
Add `--raster-python /absolute/path/to/python3` for generated scientific-processing
and infrastructure checks. Set `HELIOS_INFERENCE_PYTHON` to an explicitly prepared
CPU inference environment for generated model/proposal checks.

Screenshots require an unlocked graphical session and screen-recording access.
This is programmatic native state validation; manual pointer checks remain
necessary for dragging, resizing and menu behavior. Remove only the disposable
root you created after retaining needed evidence.

## Scientific acceptance

Unit tests and generated rasters do not establish geographic/radiometric accuracy
or detector performance. Use real, identified products and independent controls.
Retain processing versions/options, compare scientific rasters with independent
references, measure held-out registration residuals, and evaluate change proposals
against real dated events. See [SAR validation](docs/SAR-QUALITY-AND-MONITORING.md)
and the [satellite plan](docs/SATELLITE-ANALYSIS-PLAN.md).

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

GitHub Actions is configured for the Qt client on Linux and Apple Silicon macOS
runners, plus the portable shared core. No browser, Node dependency installation
or website test suite is part of the project.
