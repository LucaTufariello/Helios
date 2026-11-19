# Satellite analysis and infrastructure change review

Helios's **Satellite** section contains **Library**, **SAR**, **Optical** and **Site review**. Copernicus search/download, SAFE import, SNAP processing, saved SAR results, engine setup and exports live in SAR. The map is the shared native Qt globe, with Mission Control available in a separate window.

This is an initial **human review workflow for site-scale radar changes**. It does not classify construction, materials, vehicles, structural damage or operating status, and it does not certify radiometric/geographic accuracy. These limits remain visible in the interface and exported evidence.

## Use the workspace

1. Open **Satellite** in the left section selector or toolbar. **View → Satellite Analysis** (`⌘2`) opens the same section; `⌘1` returns to the main workspace.
2. In **SAR workflow**, download/import and process two acquisitions covering the site. Use the **same satellite, acquisition mode, orbit direction, relative orbit, output spacing, orbit option and speckle option**. Process VV and/or VH as needed. The comparison consumes scientific Sigma0, not PNGs or the false-color composite.
3. Open **Infrastructure review**. Expand **Save a new site / rename selected site**. Enter a name/type, zoom to a site a few kilometres across, and draw two opposite boundary corners or use the current view. **Save new site** stores that boundary on this Mac. Saved sites appear in the left sidebar. Drawing another boundary does not silently alter an existing saved site's boundary.
4. Select the site, the earlier **Baseline**, the **Later** acquisition, and VV or VH. Compatibility errors explain why a pair cannot be compared. With only one completed acquisition, the interface asks for a second date rather than creating substitute data.
5. Keep the initial measurement settings or adjust them: 3 dB candidate threshold, a 5 × 5 local-power mean and 25 connected pixels minimum. These are configurable review parameters, not probabilities. Select **Compare site acquisitions**. Work runs in a cancellable local background process, with validating/processing/exporting/completed/failed states.
6. Selecting a saved review clears daily imagery and restores its radar overlay. Choose **Before**, **After** or **Change** to switch the map overlay. Before/after share a display scale. Orange indicates a stronger later radar return, blue a weaker return, gray little change, and transparency missing/insufficient support. **Focus review boundary** centers the site.
7. Select a candidate patch. Inspect its mean/peak dB change, connected pixel count and approximate ground area. **Locate patch on map** draws its cyan bounding rectangle; this is not a detected building outline. Record **Follow up**, **Explained / dismiss** or **Uncertain**, notes and a dated reference URL/document citation. **Save decision** persists the review. Valid unsaved edits are also saved when leaving the review page.
8. Export the floating-point **ΔdB GeoTIFF**, the selected PNG, or **evidence & review decisions** as JSON. **Show local review files** exposes additional rendered/candidate-mask TIFFs and bounded developer diagnostics.

**Reference imagery** now includes [dated Sentinel-2 optical requests and viewing](SENTINEL2-OPTICAL.md), the existing daily NASA GIBS controls and an option to display the optical basemap without radar. The Esri basemap has varying acquisition dates; it is context, not dated proof of a change. Daily composite imagery is coarse and can have gaps. Sentinel-2 true-colour viewing supplies dated optical context; automated optical change comparison is not implemented.

## Processing architecture

- `Core/Infrastructure.{h,cpp}`: site, options, result, provenance, candidate decisions, pair compatibility and bounded atomic JSON storage.
- `Core/ScientificQueue.{h,cpp}`, `Core/WorkerJob.{h,cpp}`: cancellation/progress-safe process adapter exposes the `compare` operation. Command syntax stays behind this boundary; arguments are never shell interpolated.
- `UI/ReviewWindow.cpp`, `UI/SatelliteWindow.cpp`: saved-site/review state, background tasks, recovery, notes and exports.
- `native/workers/SAR/change_review.py`: modular pair admission, grid admission, local-power averaging, log-ratio calculation, connected-patch extraction and exports. The isolated `sentinel1.py` worker dispatches the `compare` operation.
- Existing navigation/tools and the native raster overlay host the dedicated section. All original SAR processing jobs/downloads stay in their existing locations.

External dependencies are unchanged: **ESA SNAP GPT / Microwave Toolbox** for creating calibrated/geocoded input products, plus a **Rasterio/GDAL, NumPy and Pillow** environment for comparison. Comparing two already processed acquisitions does not rerun SNAP or redownload a scene. No paid detector/API or Python GUI bridge is required.

## Measurement stages and interpretation

1. **Admit pair:** require two completed version-1 GRD jobs, valid ordered UTC acquisition dates, selected VV/VH scientific output, recorded Calibration/Terrain-Correction stages, matching satellite/mode/pass/known relative orbit and matching orbit/filter/spacing options. Same-satellite admission is deliberately conservative; cross-satellite calibration and geometry need evaluation before relaxing it. Metadata checks do not verify which precise/restituted orbit was actually available; inspect source diagnostics when accuracy matters.
2. **Admit grids:** require single-band, north-up EPSG:4326 floating-point rasters tagged Sigma0 / linear power. Require matching pixel spacing (0.1% transform tolerance). Reject dB/RGB inputs, nonlocal/symlink-escaping raster names and a site outside either raster's bounding extent.
3. **Read the site:** use Rasterio windows. Cap a review at **1,000,000 baseline pixels** and require at least nine pixels on each side. Reads never load an entire acquisition. Small bounded arrays are used for site statistics and connected components; the later source window includes interpolation support. GDAL warp memory is 64 MiB with one warp thread; this is not a total-process memory guarantee.
4. **Align power:** bilinearly resample the later **linear power** window onto the baseline grid. Nonfinite, nodata, zero and negative values are invalidated before interpolation. This grid alignment inherits the original geocoding; it is **not sub-pixel image co-registration or independent ground-control validation**.
5. **Average:** on the common validity mask, take a valid-only square local mean in linear power (3, 5 or 7 pixels). Require at least 80% valid neighborhood support and a valid central sample in both dates. Both dates use the same support. This explicit spatial averaging reduces local variability and detail; it is not an added Refined Lee filter or improved sensor resolution. Site-edge neighborhoods with insufficient support are transparent.
6. **Calculate:** `delta_dB = 10 log10(mean_after) − 10 log10(mean_before)`. Both means must be finite and positive. The result is a log ratio in dB: +3 dB is approximately twice the averaged radar power; it is not twice the amount of infrastructure. Require at least **50% common valid coverage** after support masking; report the actual fraction.
7. **Candidates:** threshold increases/decreases independently, keep eight-connected same-sign patches meeting the minimum pixel count, and cap at **500 candidates**. Rasterio/GDAL sieve/polygonization operate only on the bounded site. Save mean ΔdB, peak absolute ΔdB, pixel count, bounds and approximate spherical ground area (latitude-dependent), sorted by area. Original sign/classes are preserved after sieving. Candidate bounding rectangles may include unchanged pixels or holes.
8. **Render/export:** before/after use shared 2nd–98th percentiles of the site's valid averaged dB values, with a minimum 1 dB span. Change colors use a fixed symmetric display range of ±2 × the selected candidate threshold. Saturated colors are display clipping, not confidence. Scientific ΔdB remains Float32 with `-9999` nodata. PNGs are capped at 2048 pixels and obtain georeferencing from saved review metadata in Helios.

The [ASF change-over-time guide](https://hyp3-docs.asf.alaska.edu/guides/rtc_product_guide/#quantifying-change-over-time) explains log-ratio change measurement and seasonal effects. Its terrain-normalized products are not equivalent to this workflow's unflattened Sigma0. [Rasterio reprojection documentation](https://rasterio.readthedocs.io/en/stable/topics/reproject.html) describes the GDAL-based grid transformation used here. Review thresholds/support sizes are engineering defaults and remain uncalibrated against an infrastructure evaluation dataset.

## Storage, provenance and cleanup

Normal local storage:

```text
~/Library/Application Support/Helios/
  SAR/downloads/<acquisition UUID>/          existing downloads
  SAR/jobs/<processing UUID>/               existing scientific input products
  Satellite/sites.json                     saved named boundaries
  Satellite/reviews/<review UUID>/
    result.json                            immutable measurement + evidence snapshot
    notes.json                             analyst decisions, notes, citations, edit times
    events.jsonl                           stage timestamps / elapsed times
    delta-db.tif                           scientific log ratio, Float32 / dB
    candidate-mask.tif                      0 noncandidate/invalid; 1 increase; 2 decrease
    change-display.tif                      georeferenced RGBA display
    Before.png / After.png / Change.png     map/standard previews
    diagnostics.log                        bounded failure diagnostics, when needed
```

Each result records the saved site snapshot, acquisition/job IDs and dates, source processing options/engine/stages, polarization, analysis options, CRS/grid/bounds, display limits, valid coverage, candidates, elapsed time and explicit limitations. SHA-256 fingerprints cover the source `result.json` and the normalized aligned site-power arrays before the common mask; they are **not hashes of the entire multi-gigabyte source GeoTIFF**. Source TIFF byte lengths are also recorded. The evidence JSON retains this provenance and saved review decisions; private absolute source paths are not included.

JSON reads/writes are bounded and atomic. Unreadable prior site/notes documents are preserved; notes admission is limited to known candidate IDs with 4,000-character notes and 1,000-character citations. Unique UUID requests/review directories avoid collisions. Temporary request files are removed by the native task after success/failure/cancellation. Python removes partial rasters on failure/cancellation and retains a concise error record plus a diagnostic log capped by construction to 16,000 characters. Source acquisitions/scientific rasters are read only. Interrupted review outputs are recovered only when the saved worker PID is confirmed gone, never while another instance is still processing. An interrupted record lacking a reliable PID is preserved rather than risking deletion.

## Validation and remaining work

`make test-raster` includes generated small raster tests for a known power ratio, no change, decreases, minimum patch support, invalid pixels, linear-before-log averaging, geometry/date/settings gates, shifted grids, coverage rejection, path traversal/symlinks, partial-output cleanup and the actual isolated worker CLI. Desktop tests cover compatibility, bounded site/notes persistence, corrupt-document preservation, output admission and decoding/round-tripping the worker's scientific provenance. Generated fixtures are labeled and do not establish real-product accuracy.

Earlier validation on 2026-10-04 (historical): 119 native tests passed with 9 optional checks skipped; all 37 SAR tests passed; 3,899 legacy tests passed with one skip. The arm64 release build, strict signature verification, formatting and module-boundary checks passed. Native UI checks used an isolated, explicitly generated profile and covered site creation/drawing, acquisition comparison, before/after/change switching, candidate location, saved decisions/notes/citations, restart persistence, all three user-facing exports and optical-to-radar navigation. The exported scientific TIFF was verified as Float32/EPSG:4326 with dB units and nodata; the evidence export retained notes and provenance without private absolute paths or a worker PID. These checks establish implementation behavior, not accuracy on real infrastructure acquisitions.

For an isolated native UI profile, run:

```sh
"$HOME/Library/Application Support/Helios/SAR/runtime/bin/python3" -B \
  native/workers/validation/create_change_review_fixture.py --output /tmp/helios-review-qa
./output/desktop/build/bin/Helios --profile /tmp/helios-review-qa
```

The output directory must be new. The satellite sidebar explicitly identifies the developer profile/generated QA data. Fixture jobs never enter the user's normal library. This flag is only an explicit developer override; the application does not automatically load example acquisitions or findings. Stop the validation instance and remove its temporary profile after testing.

Still required before claiming infrastructure detection accuracy:

- A real comparable two-date acquisition pair and independently dated construction/expansion reference evidence, with stable control areas.
- Independent geographic residuals/co-registration verification and absolute radiometric reference checks; see [SAR quality checks](SAR-QUALITY-AND-MONITORING.md).
- Sensitivity/false-change evaluation across thresholds, terrain, moisture, season, vegetation and processing/orbit variants. No calibrated probability or automatic confirmed-event label is available.
- Radiometric terrain flattening and explicit layover/shadow/incidence quality masks for broader mountainous/mixed-geometry analysis. Current output remains Sigma0, not terrain-flattened Gamma0.
- The shared Qt [monitoring workbench](SITE-MONITORING.md) adds translation-only subpixel correction and independent stable-control verification. Real-data accuracy is still unevaluated. Multi-temporal/statistical baselines, optical change measurements and a full-resolution interactive raster/value inspector remain future stages.

The first feature is intended to support infrastructure investigations with measured signals and portable human-reviewed evidence. It is not a claim of Palantir Maven feature parity.

Next validation work is retained in the [satellite analysis plan](SATELLITE-ANALYSIS-PLAN.md).
