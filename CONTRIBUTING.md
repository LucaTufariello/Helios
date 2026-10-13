# Contributing to GodEye

GodEye is a native Apple Silicon macOS application. Use the setup instructions
in [README.md](README.md), then build with `make build`.

## Architecture

Keep SwiftUI/AppKit interface code in `native/macos/Sources/GodEyeMac/`, portable
validated models and services in `GodEyeCore/`, scene bridging in `CScene/`, and
renderer/dependency implementation in `native/macos/engine/`. Scientific workers
live with the app's SAR/Optical resources; invoke them through `SarProcessor`
using argument arrays, background jobs and bounded diagnostics.

Keep the map central and reuse the native design components. Long processing
and network operations must be cancellable and keep the UI responsive. Separate
reported observations, inferred/modelled values, display-only transforms and
scientific outputs. Missing data and unsupported capabilities must remain explicit.

## Validation

Run `make test`, `make test-raster`, `make format-check` and `make build` for relevant
changes. Follow [TESTING.md](TESTING.md) for scientific and opt-in UI/live checks.
Add meaningful regression tests for changed behavior; keep generated fixtures
small, identified as test data, and outside real user libraries. Report skipped
or unverified checks. GUI compilation alone does not validate the workflow.

Use `make format` for Swift formatting. Match existing Python/C++ conventions.
Do not reintroduce a browser interface, local Node server or JavaScript runtime.
Update native documentation and source/attribution metadata with behavior changes.

## Sources and contributions

Use public, accessible sources with compatible terms and retain attribution,
acquisition times, provenance and unknown values. A free endpoint does not imply
permission to redistribute its data. The canonical reference datasets and transit
registry live in `native/macos/Sources/GodEyeCore/Resources/`; see the
[resource maintenance notes](native/macos/scripts/README.md).

Keep patches scoped and describe the problem, resulting behavior, validation and
remaining limits. Never commit credentials, user mission documents, large satellite
products, generated apps or downloaded build dependencies. Report vulnerabilities
using [SECURITY.md](SECURITY.md).
