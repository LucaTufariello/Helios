# Linux hardware handoff

Updated 16 October 2026. The C++20/Qt client now implements the migration's map,
source, processing, mission and local CPU-assistance workflows. It builds and runs
on Omarchy/Wayland with actual GTX 1070 Vulkan rendering. The Swift macOS
client has been removed; the remaining platform gates are listed below.

Start with [MIGRATION-HANDOFF.md](../../MIGRATION-HANDOFF.md) and the current
[migration status](../../docs/MIGRATION-STATUS.md). The latter distinguishes
implemented workflows, generated fixtures, identified public data and unverified
platform/scientific checks.

## Transfer the current source

Use the current working tree or its matching source archive from the latest
Linux package. A fresh GitHub clone includes only revisions published there;
transfer the local commit or matching source snapshot until it has been pushed.
Package source snapshots record file hashes, the base Git revision and
working-tree status.
Retain their adjacent SHA-256 file when transferring them.

The old `output/handoff/helios-desktop-source-2026-10-13.tar.gz` contains only the
initial desktop slice. It is historical and does not contain the expanded
workflows. Preserve the Git checkout and avoid overlaying old snapshots onto it.
Build outputs, credentials, downloaded data and user profiles are separate from
source. Copied Mac experiments are optional; fresh/disposable fixtures and public
acquisitions can exercise the new client.

## Prepare Omarchy

Use Omarchy's normal system update process so Arch packages are consistent, then
install development dependencies. Keep the currently working graphics driver;
project scripts do not install or change drivers or CUDA.

```sh
sudo pacman -S --needed base-devel cmake qt6-base qt6-shadertools qt6-wayland vulkan-tools
./native/desktop/diagnose-linux.sh
make test-desktop
./output/desktop/build/bin/Helios
```

Run GPU checks inside the graphical Hyprland session. Qt private RHI headers and
libraries must match the installed Qt build. The diagnostic script only reads
OS/session/compiler/Qt/NVIDIA/Vulkan information. This PC used Qt 6.11.2 and a
local ignored `output/tools/` CMake environment because system CMake was absent.

Scientific processing requires an explicitly configured Python with the shared
SAR requirements; real GRD processing additionally requires SNAP GPT. CPU local
assistance has a separate pinned Python 3.12/3.13 environment and model contract.
Neither is required for map startup. See [local assistance](../../docs/LOCAL-INFERENCE.md).
Native camera clips require FFmpeg; the Linux package includes its native runtime.
Optional provider keys require a working Linux Secret Service/libsecret store.

## Hardware and workflow acceptance

1. Confirm the status bar reports **Vulkan** and the actual GPU. A visible window
   alone does not establish hardware rendering.
2. Navigate terrain/3D Tiles, rotate/tilt, resize and exercise wheel/trackpad/keyboard
   behavior. Check physical gestures, multiple displays/DPI and accessibility.
   The generated three-cycle travel/resource check has passed on this PC, with
   zero settled idle frames. Actual compositor minimize/restore was not acknowledged;
   hide/show passed instead. Large photogrammetry sites remain a separate stress gate.
3. Exercise Map/Satellite/Marine/Sky/Ground and Sources & observations: layer
   controls, record/map selection, retained snapshots, stale/error states, orbits
   and native camera playback. Keyed AIS/FIRMS acceptance needs real keys.
   See [source behavior and limits](DOMAIN-SOURCES.md).
4. Draw annotations, search/route, change daily visualization/display settings,
   export/import a workspace and open a `helios://` link. Public providers may be
   unavailable; preserve explicit failures. See [map tools](../../docs/MAP-TOOLS.md)
   and [daily visualization](../../docs/DAILY-IMAGERY.md).
5. In a disposable mission library, edit areas, notes/citations, tasks/due dates,
   observations/reviews and views. Retain imagery/source evidence, compare the
   timeline, run/pause an in-app monitor, then save/reopen. Monitors run while the
   app is open and do not automatically download imagery. The Monitors page's
   session-level automatic-check control also governs checkpoint updates to the
   mission library; disable it for inspection without intended monitor writes.
6. Run isolated GPU checks and inspect the reports and screenshots:

   ```sh
   ./output/desktop/build/bin/Helios --validate --qa-output output/qa/linux-generated
   ./output/desktop/build/bin/Helios --validate-live --qa-output output/qa/linux-live
   ```

   Add `--raster-python /absolute/path/to/python3` to exercise generated PRISMA
   and measured-change workers. Set `HELIOS_INFERENCE_PYTHON` to a prepared CPU
   inference environment for generated model/proposal review. Generated results
   are QA fixtures; the live command requests public map/elevation data.
7. Retrieve an identified optical acquisition and, where credentials/tools are
   available, a real Sentinel-1 GRD. Inspect provenance, bounds, VV/VH/composite,
   nodata, A/B blending and original exports. Real SNAP/DEM/calibration and a real
   compatible SAR review pair remain scientific acceptance gates. Actual CBERS
   and PRISMA source coverage also needs independent checks.
8. Build a package and repeat outside-checkout launch/worker/GPU/HTTPS/media checks
   using a clean profile. Check install/refusal/removal and profile preservation.
   See [packaging instructions](PACKAGING.md); a local artifact is not a public
   release audit.

Mission documents can be opened as a separate copy. Source raster paths must be
re-established on Linux; evidence links do not transfer image bytes. Concurrent
writers are not a joint editing protocol, so use a separate working copy for
experimentation while the application is open. Imported references can be forgotten without
source deletion; only validated owned jobs support the separate OS Trash action.
Use `--mission-library /absolute/path/to/missions.json` to open a library with
automatic monitor checks paused; resume them explicitly in the Monitors page.

## Remaining platform gates

The enabled workflow set is implemented in Qt: shared scene rendering, all 19
feeds, processing/download controls, infrastructure review, timeline/monitors,
map documents and local CPU inference. Their retained Linux evidence is listed in
migration status. No claim of complete cross-platform or scientific parity follows
from these selected checks.

Run fresh Apple Silicon Qt build/bundle/Keychain/Metal regressions after the
shared-source extraction. CI jobs are configured, but hosted
execution has not been claimed. The historical Mac slice is not acceptance of the
expanded client.

CPU is the offered Qt inference backend. The GTX 1070 is Pascal; optional NVIDIA
acceleration needs a tested compatible runtime, independently of Vulkan map
rendering. AMD rendering similarly retains the CPU baseline. No CUDA/ROCm backend,
driver change or aerial detector accuracy is claimed.
