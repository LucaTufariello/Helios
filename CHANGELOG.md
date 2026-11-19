# Changelog

## C++/Qt-only application — 19 November 2026

- Removed the Swift macOS client, its SwiftPM packages, Metal engine wrapper and
  Swift build scripts. The C++20/Qt 6 desktop client is now the only application.
- Moved the retained C++/Python/data payload into platform-neutral locations:
  `native/sgp4/`, `native/workers/`, `native/resources/` and
  `native/desktop/Tests/Fixtures/`.
- Updated CMake, Linux packaging, CI and the Makefile to the new layout; replaced
  the Swift CI job with scientific-worker tests.
- Adopted clang-format for hand-written C++ and added the application icon to the
  macOS bundle.
- Fixed macOS packaging for Homebrew Qt 6.11: quarantine storage is probed on the
  download directory itself, since the sealed system volume reports `/` as
  read-only, and the notice collector now ignores a library's own install name and
  records SPDX licence identifiers when a formula ships no licence text.
- Preserved mission, product and workspace document compatibility; the Qt suites
  own the former cross-client fixtures.
- The fallback globe is drawn as a depth-free background layer, so coarse imagery
  no longer shows dark seams where the globe's own tessellation dips close to the
  imagery surface at long camera ranges.

## Native application — 12 October 2026

- Retired the browser application, Node services, web assets, Vite/npm dependencies,
  Pinokio launchers, browser QA scripts, caches and obsolete website documentation.
- Made native bundled reference datasets and the transit registry canonical.
- Added direct `make` build/test/format commands and Apple Silicon macOS CI.
- Replaced migration/parity UI with the native About panel and current capability
  documentation. Preserved native data compatibility, licensing and source notices.
- Native map workspace and Mission Control use clearer navigation, a searchable
  imagery library, acquisition thumbnails, review/task summaries and adaptive
  light/dark surfaces.
- Optical retrieval retains bounded retries, diagnostics and cancellation cleanup
  for slow or interrupted archive reads.

Earlier implementation history remains in Git. The maintained source, testing and
installation documentation describes the native macOS application.
