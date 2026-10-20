# Mac validation handoff: connected investigations

The Linux implementation and package checks pass. Fresh execution on a Mac remains
an acceptance gate; the user has deferred it until later. Use an Apple Silicon
Mac with macOS 14 or newer and full Xcode selected as the active developer tools.
The shared Qt client contains the new workbench. The Swift reference has matching
persistence and cleanup support.

## Transfer the exact implementation

Copy these files from the Linux workspace to the Mac:

```text
output/packages/linux-investigations-20261020-complete/godeye-source.tar.zst
output/packages/linux-investigations-20261020-complete/SHA256SUMS
```

It includes the uncommitted implementation and the pre-existing source baseline;
GitHub does not need to be updated first. The archive contains a `GodEye/` folder
and no Linux build caches. Its SHA-256 is recorded on the `godeye-source.tar.zst`
line of `SHA256SUMS`. This handoff is also included inside the source archive.

## Build and run

Assuming the archive was copied to Downloads, extract into a new directory:

```sh
brew install cmake pkgconf qtbase qtshadertools zstd
shasum -a 256 "$HOME/Downloads/godeye-source.tar.zst"
mkdir "$HOME/GodEye-investigations-check"
zstd -dc "$HOME/Downloads/godeye-source.tar.zst" | tar -x -C "$HOME/GodEye-investigations-check"
cd "$HOME/GodEye-investigations-check/GodEye"
make test-desktop
open output/desktop/build/bin/GodEyeDesktop.app
```

Compare the checksum before extracting. Choose another new directory if that name
already exists. The first build downloads pinned Cesium/native dependencies. The
build creates and verifies a local ad-hoc signed Qt bundle. It also runs all 24
desktop CTest targets, including investigation core and UI tests.

Run Swift persistence tests and formatting separately:

```sh
make test
make format-check
```

`make test` builds the Swift reference's native engine and runs Swift XCTest,
including the Qt-authored investigation and registry fixture checks. Preserve the
terminal output if a check fails.

## Check the actual Metal workflow

From the extracted repository, with the desktop session unlocked:

```sh
./output/desktop/build/bin/GodEyeDesktop.app/Contents/MacOS/GodEyeDesktop \
  --validate --qa-output output/qa/investigations-mac
```

Inspect `output/qa/investigations-mac/validation.json`, generated screenshots and
the briefing folder. Confirm the renderer is Metal and that investigation checks
pass. The harness uses a disposable profile and generated civilian evaluation
data. It does not alter the normal mission library.

If scientific Python or CPU inference checks are skipped, record those skips.
For a fuller regression run, use the existing configured environments:

```sh
GODEYE_INFERENCE_PYTHON=/absolute/path/to/inference/bin/python \
./output/desktop/build/bin/GodEyeDesktop.app/Contents/MacOS/GodEyeDesktop \
  --validate --raster-python /absolute/path/to/raster/bin/python3 \
  --qa-output output/qa/investigations-mac-full
```

These two paths are placeholders for prepared environments, not installation
commands. Native PDF generation requires neither environment. An optional
`--validate-live` run checks public map/terrain access and contacts those providers.

## Check a portable case

Use disposable library copies and distinct test profiles; keep actual working
libraries separate while the Qt and Swift clients are open.

1. Open `native/macos/Tests/GodEyeCoreTests/Fixtures/cross-platform-investigation.json`
   in Qt Mission Control and save a separate working copy. Its synthetic finding
   should retain its prior decision, citations and dependent tasks. Raster files
   are intentionally unavailable.
2. Inspect Investigation, Asset registry, Connections and Findings. The case's
   snapshots must remain visible before importing anything into the Mac registry.
   Preview the registry import, inspect conflicts, then explicitly approve it.
3. Create a second mission and adopt the same asset. Edit that asset in the
   registry. Both cases should show an update notice and retain their old versions.
   Explicitly adopt the update in one case. The other case and the earlier finding
   must retain their original snapshots.
4. Change a reviewed finding's claim or citations: it should return to Unreviewed
   and retain the prior decision. Confirm Supported requires a supporting citation
   and rationale. Confirm an unfinished prerequisite blocks task completion.
5. Export explicitly selected findings with a current map capture. Inspect every
   PDF page for clipping, missing-evidence labels, source/capture dates and
   attribution. Confirm a new folder contains both PDF and JSON manifest. Export
   must leave the finding's review state unchanged.
6. Save the case/library on the Mac, copy it back to Linux, and open it using a
   disposable Linux profile. Confirm IDs, adopted versions, prior reviews,
   citations, task origins and dependencies survive. Registry import remains
   explicit; a copied mission does not transfer raster files.

Return the desktop/Swift test output, `validation.json`, screenshots and any
problematic briefing with the Mac/Qt versions. A passing Linux compatibility
harness does not replace this real Mac run.
