# Contributing to Helios

Helios is a native C++20/Qt 6 desktop application for Apple Silicon macOS and
Linux. Use the setup instructions in [README.md](README.md), then build and test
with `make test-desktop`.

## Architecture

- `native/desktop/`: Qt Widgets application, GPU map, workflow/storage modules and tests.
- `native/scene/`: shared Cesium scene adapter.
- `native/shared/`: portable C++20 geography library.
- `native/sgp4/`: native Vallado SGP4/SDP4 propagation.
- `native/workers/`: scientific Python workers and their tests.
- `native/resources/`: canonical bundled datasets and the transit registry.

Keep the map central and reuse the existing Qt design components. Long processing
and network operations must be cancellable and keep the UI responsive. Separate
reported observations, inferred/modelled values, display-only transforms and
scientific outputs. Missing data and unsupported capabilities must remain explicit.

Do not add Swift, SwiftUI/AppKit, WebEngine, Electron, a browser interface, QML,
Node.js or a local JavaScript runtime. Native platform integrations use the
existing C++/Qt/Objective-C++ boundary.

## Validation

Run `make test-shared`, `make test-desktop`, `make test-raster` and
`make format-check` for relevant changes. Follow [TESTING.md](TESTING.md) for
scientific and opt-in UI/live checks. Add meaningful regression tests for changed
behavior; keep generated fixtures small, identified as test data, and outside
real user libraries. Report skipped or unverified checks. GUI compilation alone
does not validate the workflow.

Use `make format` for the owned C++ sources (`clang-format`; Xcode's toolchain
provides it on macOS). Match existing Python conventions. Update documentation
and source/attribution metadata with behavior changes.

## Sources and contributions

Use public, accessible sources with compatible terms and retain attribution,
acquisition times, provenance and unknown values. A free endpoint does not imply
permission to redistribute its data. The canonical reference datasets and transit
registry live in `native/resources/`; see the
[resource maintenance notes](native/resources/README.md).

Keep patches scoped and describe the problem, resulting behavior, validation and
remaining limits. Never commit credentials, user mission documents, large satellite
products, generated apps or downloaded build dependencies. Report vulnerabilities
using [SECURITY.md](SECURITY.md).
