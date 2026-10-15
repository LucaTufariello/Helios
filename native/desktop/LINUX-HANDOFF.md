# Linux hardware handoff

Prepared October 13, 2026. This handoff covers the first C++20/Qt desktop slice.
The full Swift app remains usable on macOS while the remaining workflows migrate.

For an API-based coding model continuing the full migration, start with
[MIGRATION-HANDOFF.md](../../MIGRATION-HANDOFF.md). It includes the remaining
workflow inventory, implementation order and both-platform acceptance checks.

## Transfer the current source

The desktop baseline was committed locally as `f40d08a`; publication was left to
the user. Verify the Linux checkout contains that commit and the newer root
`MIGRATION-HANDOFF.md`. An older remote revision will not include this work.
The source-only snapshot prepared before that commit is at:

`output/handoff/godeye-desktop-source-2026-10-13.tar.gz`

Copy it to the PC and extract it into a new folder. The snapshot includes the
current native source, tests, fixtures and documentation, with no Git history,
compiled Mac binaries, credentials, downloaded satellite datasets or user mission
library. It omits the retired website. Keep the Mac checkout as the Git working
copy; use the snapshot as a separate test checkout, not an overlay on older code.
The adjacent SHA-256 file can verify transfer integrity. This October 13 snapshot
does not contain later handoff documentation or subsequent changes; prefer the
updated Git checkout once the user has pushed it.

## Prepare Omarchy

Use Omarchy's normal system update process first so Arch packages are consistent.
Install the development dependencies below. Leave the currently working NVIDIA
driver in place; the project scripts do not install or change GPU drivers or CUDA.

```sh
sudo pacman -S --needed base-devel cmake qt6-base qt6-shadertools qt6-wayland vulkan-tools
./native/desktop/diagnose-linux.sh
make test-desktop
./output/desktop/build/bin/GodEyeDesktop
```

Run these inside the graphical Hyprland session. The map selects Vulkan on Linux.
Qt private RHI headers/libraries must come from the same installed Qt build. The
diagnostic script only reads OS/session/compiler/Qt/NVIDIA/Vulkan information.

## Hardware acceptance

1. Confirm the status bar reports **Vulkan** and the actual NVIDIA GPU; a running
   window alone does not prove hardware rendering.
2. Navigate the real basemap and terrain, rotate/tilt, resize the window, and check
   wheel/trackpad/keyboard behavior in Wayland. Check that repeated navigation does
   not keep increasing memory and that an idle map does not redraw continuously.
3. Open Mission Control. Add an area, view, note and task; focus an area on the
   shared map. Save/reopen a disposable mission library.
4. Run the isolated GPU checks from the graphical session:

   ```sh
   ./output/desktop/build/bin/GodEyeDesktop --validate --qa-output output/qa/linux-generated
   ./output/desktop/build/bin/GodEyeDesktop --validate-live --qa-output output/qa/linux-live
   ```

   Inspect `validation.json` and screenshots. Generated imagery is labeled QA;
   the live run requests actual public map and elevation tiles. Retain a failure
   report rather than treating a fallback or missing frame as a pass.
5. Copy one completed optical and one SAR job folder if wanted. Preserve their
   `result.json` and referenced preview/GeoTIFF filenames. Import these folders
   in Satellite, check VV/VH/Composite, blend two previews and export original
   files. Use `--validate --product /absolute/path/to/result.json` with a separate
   QA output folder for a read-only product display check.

Mission documents can be opened as a separate copy in the preview. Source raster
paths and imports must be re-established on Linux; a mission evidence link does
not include the image bytes. Do not share the same live mission file between the
Swift and Qt clients: their file locks do not form a joint editing protocol.

## What follows acceptance

Photorealistic 3D Tiles, catalogue/download/processing controls, remaining public
feeds, monitoring, inference and full workflow parity still need migration.
The existing Python/SNAP scientific workers remain the processing implementation;
the Qt preview currently imports their completed results.

CUDA is optional and independent of map rendering. The GTX 1070 is Pascal; future
GPU inference needs a tested compatible dependency set, rather than assuming the
newest CUDA/PyTorch package supports it. AMD users should retain Vulkan rendering
and a CPU inference baseline. No inference backend is claimed tested by this handoff.

The Mac tests establish working Metal frames, portable document/product behavior
and regression compatibility. Linux compilation, Wayland input, Vulkan on the GTX
1070 and Linux packaging have not yet been verified on the target PC.
