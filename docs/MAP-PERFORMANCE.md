# Map navigation performance

The Linux map performance work on 16 October 2026 targets CPU stalls while
navigating an already loaded map. The investigated machine runs Omarchy/Arch,
Hyprland/Wayland, Qt 6.11.2 and Vulkan on an NVIDIA GeForce GTX 1070 with 8 GiB
VRAM and approximately 15 GiB system RAM.

The status label's loaded-tile count and zero pending requests confirm that
imagery is available; they do not measure frame latency. Profiling isolated
expensive work on the application's CPU/render thread: repeated elevation
lookups, whole-surface rebuilds during terrain arrivals, per-marker camera
calculations and triangle searches during picking. Additional RAM does not
shorten those operations. The results below describe this machine and generated
workload; they do not establish a hardware comparison with earlier Mac measurements.

## Changes

- Terrain sampling uses a numeric index of loaded elevation tiles, checks only
  available zoom levels, and calculates the Mercator position once per lookup.
  An empty terrain cache returns immediately. The existing 224-entry cache owns
  the indexed data; replacement and eviction update the index together.
- Terrain arrivals are grouped within an 80 ms window before rebuilding map
  geometry. Camera clearance still updates when terrain arrives. Existing
  basemap textures are reused when only the surface geometry changes.
- Marker drawing reuses the camera basis and projection across a layer. A
  marker with an explicit altitude does not calculate an unused terrain fallback.
- Picking rejects meshes whose bounds cannot intersect the ray before testing
  their triangles. Accepted hits still use rendered triangles, refinement masks
  and product transparency.
- Tile requests track decoding work so the same tile is not requested again
  while its background decode is pending.

These changes retain map level-of-detail selection, terrain tessellation, tile
image resolution, scientific raster values, product colours and native GPU
rendering. They do not require CUDA, different drivers or desktop configuration.

## Measurements

The accepted comparison uses a fixed 1100 × 760 logical/pixel window at scale 1,
76 generated imagery tiles, flat generated 120 m terrain, identical camera paths
and 300/3,000-marker cases. Both runs use the same benchmark source and 14
correctness checks. Runs are serial in the real graphical session.

| Work | Before | After | Measurement |
| --- | ---: | ---: | --- |
| Initial imagery upload | 930.98 ms | 25.85 ms | CPU render duration; one frame per run |
| Terrain arrivals | 928.65 ms | 27.34 ms | CPU render duration, 95th percentile |
| Pan/rotate with 300 markers | 3.97 ms | 0.82 ms | CPU render duration, 95th percentile |
| Pan/rotate with 3,000 markers | 33.84 ms | 2.56 ms | CPU render duration, 95th percentile |
| Pan/rotate with 3,000 markers | 38.07 ms | 16.94 ms | Frame spacing, 95th percentile |
| Repeated surface picking | 9.66 ms | 1.18 ms | Per-pick duration, 95th percentile; 60 picks |

Warm navigation without markers already had approximately 0.4 ms median CPU
render work before and after. The largest improvements are during terrain
loading, dense marker rendering and picking. Both runs produced zero frames in
the settled-idle phase. Terrain batching changes the number of rendered arrival
frames (19 before, 24 after, including inexpensive updates); both runs insert
the same 76 terrain tiles and complete the same camera updates and valid picks.

`renderMS` measures CPU work and GPU command recording inside `MapWidget::render`;
it is not GPU execution time. Paint, frame-spacing and timer-spacing measurements
also expose event-loop and compositor pacing. The benchmark requests camera
updates every 16 ms; faster render work does not imply an unlimited frame rate.

Evidence is local and ignored by Git under
`output/qa/map-performance-20261016/`. Use only the `baseline-fixed/` and
`optimized-fixed/` reports for the accepted comparison. Earlier `baseline/` and
`optimized/` runs were affected by compositor window resizing and are retained
with `NOT-COMPARABLE.txt`; their ratios are not release results. Reports retain
source and executable hashes, actual window/framebuffer dimensions, tile IDs,
correctness checks and sample distributions.

The updated build passed all 17 CTest suites, 104 native GPU integration checks
and 19 live map/terrain/daily-imagery checks. The extended GPU resource scenario
passed 129 checks, including known-height picks at tile boundaries, transparent
products, antimeridian/high-latitude travel and deferred status callbacks. Three
minimize checks were skipped because Hyprland did not acknowledge the requested
Qt minimized state; hide/show checks passed. This run peaked at approximately
269 MiB resident memory. Terrain unit tests cover bilinear pixel-centre sampling,
level fallback, antimeridian/polar edges, replacement and bounded-cache
eviction/reload. The corresponding reports are in `native/`, `live/` and
`resources/` under the evidence root above.

## Run the benchmark

After building the desktop client, run from the repository root inside a working
graphical session:

```sh
cmake --build output/desktop/build --target HeliosMapNavigationChecks --parallel 2
HELIOS_NAVIGATION_CHECK=1 \
  HELIOS_NAVIGATION_QA="$PWD/output/qa/map-navigation-local" \
  ./output/desktop/build/bin/HeliosMapNavigationChecks
```

Choose a separate output directory for each run. The benchmark opens a native
window, generates its own imagery/elevation/markers and makes no provider
requests. Run one GPU check at a time with the same display, scale, window size
and desktop activity. Inspect `map-navigation-checks.json` and
`generated-navigation.png`. Timing values are observational; correctness checks,
not machine-specific timing thresholds, determine its exit status.

For a source comparison, set the optional CMake cache variable
`HELIOS_NAVIGATION_BASELINE_ROOT` to an independent snapshot containing
`Graphics/MapWidget.cpp`, `Graphics/MapWidget.h`, `Graphics/TileCache.cpp` and
`Graphics/TileCache.h`. CMake adds `HeliosMapNavigationBaseline`, which runs the
same harness against those frozen graphics sources. Verify benchmark hashes,
camera/tiles, platform/backend/device, scale and window/framebuffer dimensions
match before comparing distributions.

The ordinary regression command remains `make test-desktop`. The opt-in timing
benchmark is outside CTest. Its generated scene does not cover provider latency,
physical mouse/trackpad input, multiple displays, all DPI scales, a large city
tileset or fresh Mac/Metal execution.
