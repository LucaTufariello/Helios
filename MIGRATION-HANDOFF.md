# Native desktop migration: completion record

The migration from the earlier Swift macOS application to a single C++20/Qt 6
desktop application is complete in the working tree. The Swift client, its SwiftPM
packages, Metal engine wrapper and build scripts were removed; the C++ client is
now the only application. The removed sources remain available in Git history
before commit `5779148`.

This file replaces the earlier migration handoff. It records where the retained
pieces live and how to verify the current tree. Current capabilities, limits and
acceptance evidence are in [docs/MIGRATION-STATUS.md](docs/MIGRATION-STATUS.md)
and [docs/CURRENT-STATE.md](docs/CURRENT-STATE.md).

## Repository layout

| Path | Contents |
| --- | --- |
| `native/desktop/` | Qt 6 Widgets application, validation harness, CTest suites, packaging |
| `native/scene/` | Cesium Native scene adapter and Qt RHI transport |
| `native/shared/` | Portable C++20 WGS84/geometry library with a C ABI |
| `native/sgp4/` | Vallado SGP4/SDP4 propagation (pinned upstream revision) |
| `native/workers/` | Scientific Python workers, tests and validation scripts |
| `native/resources/` | Canonical bundled datasets, transit registry and attribution |
| `native/desktop/Tests/Fixtures/` | Portable mission/product fixtures used by the Qt suites |

Build and test entry points are `make test-shared`, `make test-desktop`,
`make test-raster`, `make format-check` and `make setup-raster`. See
[TESTING.md](TESTING.md) and the [desktop guide](native/desktop/README.md).

## What was preserved

- Mission documents, optical/SAR library layouts and workspace files keep their
  existing fields. The Qt client reads the same `plans.json`, job folders and
  portable exports the earlier client wrote, preserves unknown extension fields
  and Foundation timestamps, and writes atomically with backups.
- Scientific workers (`sentinel1.py`, `sentinel2.py`, `optical_sources.py`,
  `change_review.py`, `registration.py`) moved unchanged in behavior from
  `native/macos/.../Resources` to `native/workers/`. Their process contract,
  arguments, outputs and provenance are unchanged.
- The SGP4 implementation and its reference vectors moved to `native/sgp4/` and
  `native/desktop/Tests/Fixtures/sgp4-reference.json`.
- Bundled reference data moved to `native/resources/` and is staged into the
  application by CMake. Resource READMEs, licenses and attribution were retained.
- The macOS bundle still deploys Qt frameworks, the worker host, workers, sources
  and notices, and remains ad-hoc signed for local use.

## Deliberately removed

- `native/macos/` Swift sources, SwiftPM manifests, the Swift Metal engine wrapper
  (`engine/`, `Sources/CScene/`), `Globe.metal`, `Icon.swift`, `build-app.sh`,
  `build-engine.sh`, `test.sh`, `test-sar.sh`, `setup-sar.sh` and `format.sh`.
- The Swift-specific CI job and Swift build caches in `.gitignore`.
- The cross-client round-trip tests that required a Swift reader/writer. Document
  compatibility is now covered by the Qt suites and the fixtures above.

## Verification

```sh
make format-check
make test-shared
make test-desktop
make setup-raster   # once, then:
make test-raster
```

`Helios --validate --qa-output output/qa/desktop --raster-python
/absolute/path/to/raster/bin/python3` exercises the native workflows with the GPU.
Without `--raster-python` the harness skips every generated scientific job, so the
worker, dossier and monitoring checks do not run. Fresh Apple Silicon
Metal/Keychain and large-dataset acceptance gates remain listed in the migration
status; this record does not claim them.
