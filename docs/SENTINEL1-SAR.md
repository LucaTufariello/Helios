# Sentinel-1 GRD processing in the native macOS app

## Architecture and scope

The production application is `native/macos`: Swift 6, macOS 14+, arm64, SwiftUI/AppKit, Metal and Foundation. The interface, orchestration and storage are native; optional scientific workers are local background processes.

The existing native application uses `NSOpenPanel` / `NSSavePanel`, Swift tasks, Codable JSON documents, and local Application Support files. It has no general durable job queue or database. SAR follows these conventions:

- `GodEyeCore/SarProcessor.swift`: typed product/options/result/output/progress models, asynchronous worker abstraction, safe process arguments, cancellation and interrupted-job recovery.
- `GodEyeMac/SarWorkspace.swift`: native import, configuration, status, result browser, preview, export and job storage controller.
- `GodEyeMac/Resources/SAR/sentinel1.py`: local worker with discrete admission, inspection, graph construction, processing, raster conversion and output stages.
- `GlobeRenderer.swift`: a bounded georeferenced PNG display overlay in the existing Metal scene. It uses the **terrain-corrected output's actual EPSG:4326 bounds**, not the raw SAFE measurement footprint.

The pipeline currently supports Sentinel-1 A/B/C/D Level-1 **GRD** SAFE products, IW/EW/SM acquisition modes, one measurement per available VV/VH polarization. Mission parsing accepts C/D identifiers, but compatibility with actual C/D acquisitions still depends on the installed Microwave Toolbox and needs real-product validation. HH/HV-only products, SLC, OCN, InSAR and advanced classification are explicitly outside this implementation.

## Engine selection and dependencies

ESA SNAP **GPT with the Microwave Toolbox** performs the SAR-specific work. SNAP supplies mission readers, calibration/noise lookup-table interpolation, orbit handling, Refined Lee filtering and range-Doppler terrain correction. Implementing these from raw TIFF pixel contrast, or substituting GDAL reprojection for radar geocoding, would not be scientifically correct.

[ESA's current download page](https://step.esa.int/main/download/snap-download/) provides an Apple Silicon installer. Use **SNAP 14 or later**, including its current Microwave Toolbox updates. The [14.0 Microwave release notes](https://step.esa.int/main/wp-content/releasenotes/Microwave/Microwave_14.0.0.html) describe thermal-noise changes and newer mission support. The worker records the core module version when a conventional SNAP installation exposes it; a recognized version below 14 is rejected. Other layouts record an unknown version, so an unknown version is **not** evidence of compatibility.

Rasterio/GDAL reads and writes already-geocoded raster blocks; NumPy performs dB and stretch calculations; Pillow writes bounded PNG previews. xarray and pyroSAR were considered, but neither is required: there is no multidimensional/time-series workflow yet, and this small adapter already owns SNAP graph construction and task orchestration. Rasterio itself does not calibrate Sentinel-1 or terrain-correct raw radar geometry.

Requirements:

1. An arm64 Python interpreter; Python 3.11+ is recommended. The local validation environment used Python 3.9, NumPy 2.0.2, Rasterio 1.4.3 / GDAL 3.9.3 and Pillow 11.3.0.
2. Rasterio, NumPy and Pillow, installed in a dedicated virtual environment. Their tested versions are recorded in `Resources/SAR/requirements-tested.txt`; the setup script uses the maintained compatible version ranges in `requirements.txt`.
3. SNAP 14+ with the Microwave Toolbox and an accessible `bin/gpt`. SNAP's installer supplies its Java runtime; no SNAP GUI is embedded or launched by GodEye.
4. Sufficient disk space for input extraction when zipped, SNAP intermediates and floating-point outputs. Processing may need many gigabytes and substantial CPU time.
5. Network access for precise orbit and Copernicus DEM downloads, unless SNAP already has the required auxiliary data cached. Download availability/access conditions are controlled by SNAP and the data providers.

Install the raster environment from the repository:

```sh
native/macos/setup-sar.sh
```

Optional environment variables: `GODEYE_SAR_PYTHON` chooses the interpreter for creating the environment; `GODEYE_SAR_RUNTIME` chooses its location. The default runtime is `~/Library/Application Support/GodEye/SAR/runtime`. These dependencies are installed explicitly by the setup script; importing a dataset does not install software.

Install SNAP separately from ESA, then open **SAR → Processing engine setup** and choose the environment's `bin/python3` and SNAP's `bin/gpt`. GodEye saves these non-secret configuration paths locally. External tools are not bundled into `GodEye.app`; distributing SNAP/GDAL/Python alongside the app would require separate packaging, licensing and signing work. SNAP has its own GPL licensing; the project's MIT license does not replace dependency or data licenses.

## Search and download inside GodEye

The SAR panel has two native workflows: **Find & download** and **Process & results**. No browser renderer, web server, additional download dependency or paid API is used.

1. Choose **Select region on globe…**, then click two opposite corners. The existing native surface picker supplies geographic coordinates; the selected rectangle is outlined on the Metal globe. Escape or Cancel exits selection. Alternatively, use **Use current view** (the existing viewport/frustum bounds calculation) or enter west/east longitude and south/north latitude under **Enter exact bounds**.
2. Choose an inclusive UTC date range, IW/EW/SM acquisition mode, and VV, VH or VV + VH. Search uses the public [Copernicus OData catalogue](https://documentation.dataspace.copernicus.eu/APIs/OData.html), with collection, GRD product-name, polarization, sensing-date and geographic-intersection filters. Searches are limited to a rectangle up to 10 degrees wide/high within ±85 degrees, a one-year date interval, and 20 products per page (at most 1,000). Antimeridian-crossing regions are not supported.
3. Select an acquisition. Rows show actual catalogue time, satellite, mode, polarization, orbit, availability and catalogue size. **Show scene footprint on globe** displays its supplied geographic footprint. Catalogue size is an estimate for disk planning; the transferred ZIP size can differ.
4. Expand **Copernicus account**, enter the existing free account's username/password and optional six-digit two-factor code, then sign in. Searching needs no account; downloading requires one. Account creation, if needed, still uses Copernicus's registration site.
5. Choose **Download & import**. The app streams the complete product ZIP to local storage, reports received bytes (and percentage when HTTP length is known), supports cancellation, and automatically opens **Process & results** for existing SAFE validation. Processing itself remains an explicit user action. Saved downloads can be reused without redownloading.

Region selection finds complete intersecting acquisitions: it does **not** crop products, change radar georeferencing, or request a server-side SAR processing job. Downloading a scene can require several gigabytes even for a small search rectangle. An unavailable scene cannot be downloaded. Product name filtering intentionally excludes SLC, newer COG-packaged variants and HH/HV-only products that the existing processor does not support.

`GodEyeCore/SentinelCatalog.swift` owns typed region/query/scene models, catalogue requests, an actor-isolated account session, streaming download delegate and provenance records. `GodEyeMac/SarAcquisitionWorkspace.swift` owns native search/login/download UI state and handoff to `SarWorkspace.inspect()`. Region picking reuses `WorkspaceModel.mapClick`, `GeoBounds.viewport`, existing `ScenePath` rendering and the native Metal surface picker.

Authentication follows [Copernicus's documented token flow](https://documentation.dataspace.copernicus.eu/APIs/Token.html): HTTPS form-encoded `cdse-public` password grant with optional `totp`, followed by refresh-token renewal when necessary. Password fields are cleared after submission. Passwords, access tokens and refresh tokens stay in memory and never enter UserDefaults, workspace exports, job records, logs or downloaded-product metadata. Sign out clears the session; restarting requires sign-in again. Server authentication bodies are never presented to users. Generation checks prevent an interrupted sign-in from restoring a signed-out session.

Downloads use the documented `download.dataspace.copernicus.eu/odata/v1/Products(<UUID>)/$value` endpoint. The regular endpoint supplies a zipped SAFE without relying on the separate native-compressed `$zip` endpoint's limited retention. A serial URLSession delegate writes incoming chunks directly to a unique partial file, with a 40 GiB cap, initial disk-capacity check, HTTP status/length validation and ZIP header/end-record checks. Only exact HTTPS download/catalogue service hosts may receive a forwarded Authorization header during redirects; unexpected hosts are rejected. Cancel/failure removes partial files; complete files are published by a filesystem move and never silently overwritten. Interrupted partials are removed on restart only when their recorded owner process is confirmed gone. A lost connection currently requires restarting the download; byte-range resume is not implemented. Downloads run while the application is open, not as an OS background transfer after quitting.

Completed downloads live at `~/Library/Application Support/GodEye/SAR/downloads/<catalogue-UUID>/product.zip`, with `download.json` containing the source scene, selected search query and completion timestamp. Stored paths derive from a validated UUID rather than a remote filename. Authentication is not included. The full existing SAFE inspector performs ZIP CRC, path, XML, product and measurement admission before processing. Transport completion/ZIP framing alone is not scientific validation; catalogue checksums are not assumed to describe the dynamically packaged download bytes.

Tests use generated ZIP bytes and mocked HTTP responses for query construction, metadata/polarization admission, login encoding, token refresh, credential-safe errors, redirect policy, streamed transfers, unknown/known size limits, truncated archives, response length mismatch, cancellation and failure cleanup. `GODEYE_LIVE_CHECKS=1 native/macos/test.sh --filter SentinelCatalogTests/testLiveCopernicusCatalogue` checks the real public catalogue. Authenticated acquisition download and real SAR scientific acceptance still require an account and a representative actual product.

## Scientific pipeline

For the reproducible numerical/calibration checks and the remaining independent-reference validation work, see [SAR quality and monitoring](SAR-QUALITY-AND-MONITORING.md). Those checks do not certify absolute radiometric or geographic accuracy.

1. **Validate / inspect:** accept a `.SAFE` directory, `manifest.safe`, or ZIP containing exactly one SAFE. Read the manifest, annotation, calibration and noise XML; identify matching measurement TIFFs, VV/VH, mission, mode, acquisition times, orbit direction, relative orbit and source dimensions/spacing. This is structural admission, not certification that every measurement/LUT value is sound. SNAP performs the authoritative product read.
2. **Prepare:** read directory inputs in place; extract an admitted ZIP once into the job's private temporary directory. Keep the original input reference without copying an entire unzipped SAFE.
3. **Orbit (default on):** `Apply-Orbit-File`, Sentinel Precise auto-download, `continueOnFail=false`. SNAP can fall back to restituted orbit data when precise data is unavailable; the UI discloses this and the diagnostic log records the selected orbit. If correction cannot succeed, the job fails. Turning this option off explicitly uses the product's existing orbit vectors and is recorded in the parameters.
4. **Border and thermal noise:** SNAP `Remove-GRD-Border-Noise` followed by `ThermalNoiseRemoval`, restricted to selected polarizations. Already noise-corrected bands skip thermal removal, based on their annotation; mixed correction states must be processed separately. Negative noise-corrected values are **not clipped before downstream averaging/resampling** (`clipNegativeValues=false`). See ESA's [thermal-noise operator documentation](https://step.esa.int/main/wp-content/help/versions/13.0.0/snap-toolboxes/eu.esa.microwavetbx.sar.op.calibration.ui/operators/ThermalNoiseRemoval.html).
5. **Calibration:** SNAP `Calibration`, Sigma0 output, `outputImageScaleInDb=false`. Mission calibration metadata, not an arbitrary DN scaling factor, controls conversion.
6. **Optional speckle reduction:** SNAP `Speckle-Filter` with **Refined Lee**. It is disabled by default. No Gaussian blur is labeled a SAR filter. Filtering changes the retained processed Sigma0 values; the original SAFE remains untouched.
7. **Geocode / terrain correct:** SNAP [range-Doppler Terrain-Correction](https://step.esa.int/main/wp-content/help/versions/13.0.0/snap-toolboxes/eu.esa.microwavetbx.sar.op.sar.processing.ui/operators/RangeDopplerGeocodingOp.html), Copernicus 30 m Global DEM, bilinear DEM/image resampling, EPSG:4326 and configurable nominal spacing of 10–100 m. `nodataValueAtSea=true` masks places without elevation, including some water areas. Water is retained where the DEM provides a valid height; unavailable elevation is not replaced by an invented height. Radiometric terrain normalization/terrain flattening is not applied; this is Sigma0, not terrain-flattened Gamma0.
8. **Scientific export:** branch explicitly into a single `Sigma0_VV` or `Sigma0_VH` band before SNAP's GeoTIFF-BigTIFF write. The worker requires a single Float32 band and valid north-up EPSG:4326 grid, then writes tiled, compressed scientific TIFFs in 256×256 windows. It validates matching VV/VH grids before making a composite.
9. **dB:** `10 * log10(linear)`, only for finite positive values. Zero, negatives, nodata and nonfinite values become nodata in the dB product. Finite zero/negative residuals remain in the **linear scientific TIFF** when they are not declared source nodata; they are never discarded solely to make a preview. Invalid/nonfinite source values become `-9999` nodata.
10. **Visualize:** configurable 2nd–98th percentile defaults, using deterministic nearest-neighbor sampling of at most 512×512 valid dB pixels. Constant or nearly uniform channels receive a minimum 1 dB display span, avoiding amplification of floating-point roundoff. This is a display estimate, not an exact full-population percentile. Create full-size RGBA display GeoTIFFs with transparent invalid pixels and PNG previews capped at 2048 pixels on the longer edge.
11. **Composite:** red = VV dB, green = VH dB, blue = VV dB − VH dB (equivalent to `10 log10(VV/VH)` for positive values). Each channel is independently percentile-stretched. Subtraction in dB avoids unstable linear division. Both polarizations must be valid at a pixel. The modular `composite_channels()` function can be replaced without changing calibration/geocoding.

Grayscale and false color represent **radar backscatter, not conventional optical photographs**. Calm water generally has low returns; strong urban/metallic reflections can be bright; vegetation/rough surfaces vary with polarization and geometry. Speckle, incidence angle, layover, shadow, noise and the chosen display limits affect appearance. Colors do not directly identify material or land cover.

## Native workflow and outputs

Open **Satellite → SAR workflow** in the native workspace. Import a supported product, inspect its metadata, select VV/VH/both, orbit/filter/spacing/stretch options, then **Process Sigma0**. Status covers queued, validating, processing, exporting, completed and failed. The SNAP graph runs lazily across operators, so its internal processing phase is deliberately **indeterminate**; the app does not invent per-operator percentages. Windowed export reports actual completed block progress.

Completed jobs remain in the SAR result browser. Switch between VV, VH and Composite, inspect the preview, show/hide its globe overlay, focus the result, and export:

- `<pol>-sigma0.tif`: Float32 geocoded linear Sigma0; dimensionless calibrated power.
- `<pol>-db.tif`: Float32 Sigma0 dB; invalid/nonpositive values are nodata.
- `<pol>-display.tif`: georeferenced 8-bit RGBA grayscale visualization.
- `Composite-display.tif`: georeferenced 8-bit RGBA false color when both channels were selected.
- `<output>-preview.png`: bounded standard display image. **PNG is not independently georeferenced**; the app associates it with the validated TIFF bounds in the job metadata.

TIFF tags include units and processing metadata/parameters without private absolute input paths. Job-local `result.json` additionally retains the original input path, dates, acquisition metadata, engine identification, stage list, raster dimensions/CRS/actual degree spacing, display ranges, nodata, options and timings. The input path remains private to local job storage. Scientific export does not convert Float32 to 8-bit. File exports run off the UI thread and stage replacements so a failed copy preserves an existing destination.

The globe uses a bounded display preview, not the full scientific raster, with EPSG:4326 pixel outer-edge bounds and the exact uploaded basemap/terrain triangle positions. It preserves false colors independently of the globe's simulated display shaders. Its 2048-pixel cap and the loaded terrain grid limit detail; it is a contextual display, not a full-resolution analytical raster viewer or pixel-value sampling tool. Scientific TIFFs retain the full output grid. Antimeridian-crossing scenes and output extents beyond 85° latitude are currently rejected for this overlay/export workflow.

## Jobs, security and cleanup

Local job directory: `~/Library/Application Support/GodEye/SAR/jobs/<UUID>/`.

The Swift process adapter passes a fixed operation and argument array to an isolated Python interpreter (`-I`); Python passes an argument array to GPT. Neither constructs a shell command from product names/options. Workspace state links and exported workspace documents do not include SAR paths, datasets or processing results; results are separately persisted local jobs.

Admission limits are 20,000 files, 40 GiB declared product size, 16 MiB per XML document, 250,000 XML elements, and a maximum ZIP compression ratio of 1000. ZIP traversal/absolute/backslash paths, URI-encoded/query/fragment/control-character paths, links/special files, encryption, duplicate/case-colliding paths and entries outside the selected SAFE are rejected. XML is UTF-8, and DTD/entity declarations are rejected before parsing, including auxiliary XML/KML. Manifest file references must stay within the admitted product. Extraction uses exclusive file creation and streaming writes, with CRC checking and a declared-size bound. Preflight and exact-grid disk estimates reject insufficient space.

SNAP has a 512 MiB tile cache, four worker threads and a six-hour job timeout. Its JVM heap comes from the SNAP installation's VM configuration: the tile-cache limit is **not** a total process-memory limit. Log output is monitored and capped to 16 MiB; ordinary errors show short diagnostic summaries, while detailed logs remain local. Raster conversion works in 256×256 windows under Rasterio/GDAL's normal block cache; previews and percentile samples are bounded.

Cancellation terminates the Python worker, whose handler terminates the GPT process group, escalates to SIGKILL if necessary, and cleans extracted/intermediate/partial output files. Success also deletes intermediates; diagnostics, reproducible graph template, structured `events.jsonl` and the result record remain. Interrupted active jobs are recovered on app startup only when their recorded worker PID is confirmed gone, preserving a still-running worker from another app instance. PID reuse can defer recovery rather than risking deletion. User inputs are never deleted.

These checks reduce common file admission risks; they are not a sandbox or a proof that native/JVM TIFF/XML/SAR decoders are immune to malicious content. Processing runs with the local user's permissions. Distribution to untrusted multi-user environments would need a restricted worker and additional resource/security isolation. No multi-user upload service is introduced here.

## Validation and current limits

- Small generated SAFE structures, measurement TIFFs and already-geocoded floating-point raster fixtures test admission, metadata, graph construction, numerical conversion, export, output transparency, cancellation and cleanup. Fixtures are explicitly test data; they do **not** validate real SAR calibration or terrain correction.
- Tests mock SNAP only to exercise orchestration/failure/completion. A real subprocess fixture verifies GPT-child termination and intermediate cleanup on cancellation.
- Swift tests cover state transitions, result-file admission, bounded metadata reads, missing runtime and interrupted-job recovery. The existing native and legacy suites remain regression gates.
- The raster environment was installed and exercised locally on arm64. The initial implementation used fixtures. Subsequently a real Sentinel-1D GRD acquisition completed the SNAP pipeline, and export/LUT consistency checks passed; see [the current quality report](SAR-QUALITY-AND-MONITORING.md). Independent absolute radiometric and geographic accuracy remain unverified. Successful processing and those numerical checks do not certify scientific accuracy.
- Real native UI validation may require an unlocked desktop. Build/tests alone do not establish visual QA.
- No uncertainty model, radiometric terrain flattening, incidence normalization, layover/shadow classification, geoid processing outside SNAP, AOI subset or offline bulk downloader is implemented.
- Recovery/export behavior was tested, but full disk-exhaustion, decoder-adversarial and long-running multi-gigabyte workloads need further validation. Only one job is processed at a time in each native workspace.

Local validation on October 4, 2026: **21 Python worker tests passed**, and the native suite passed **86 deterministic tests** (94 total, eight live-provider tests intentionally skipped). Six native tests specifically cover the SAR adapter models/storage boundary. The retained legacy suite passed **3,899 tests**, with one skipped and no failures. Swift strict formatting, the repository's 890-file formatting check, shell syntax and arm64 release/signature checks passed. Existing native dependency packaging emits empty-object archive notices; no new Swift compile warnings were introduced.

Native UI checks exercised generated SAFE structural import and VV/VH detection, queued/failed states for missing SNAP, saved result browsing, VV/composite selection, stable near-constant composite stretching, native PNG export, georeferenced globe focus, terrain-aligned overlay rendering and overlay removal. The display-only result was explicitly labeled as generated test data. Visual depth gaps prompted replacement of the separate overlay grid with the exact existing terrain meshes. Both test-only job directories, their temporary SAFE/raster files and the test export were removed afterward. The application and worker were left stopped. These UI checks do not establish real SAR processing accuracy.

Run the tests:

```sh
native/macos/test.sh
native/macos/test-sar.sh
native/macos/build-app.sh
```

`GODEYE_SAR_PYTHON=/absolute/path/to/python3 native/macos/test-sar.sh` selects a test environment. Generated fixtures are temporary and no satellite-sized acquisition is committed.

For a real-product acceptance run: install updated SNAP 14+, import an official VV/VH IW GRD acquisition, keep precise orbit on, process with and without Refined Lee, compare Float32 pixel samples/CRS/geographic alignment to an independently processed SNAP reference, verify nodata and water behavior, export/open results in a GIS, then exercise cancellation and an unavailable DEM/orbit case. Record the product identifier, exact engine/toolbox version, graph/options and evidence before claiming scientific validation.

## Extending the pipeline

Keep future SLC/InSAR/coherence/change-detection work behind a processor abstraction; do not treat GRD phase as recoverable. Add a stage in `build_graph()` (or a future processor), explicit options/provenance, stage diagnostics, and separate numerical/real-reference tests. Add visualization algorithms to `composite_channels()` / display functions without altering retained scientific values. If adding a CRS, adapt bounds/warping and the viewer together rather than inventing georeferencing. Metadata/time-series storage can evolve from individual job records without changing workspace scene code.

Download feature validation: the native interface was exercised against live catalogue results, including two-corner selection, current-view bounds, acquisition selection, footprint focus, the native account form and the retained processing panel. Authentication/download transport is covered by mocked responses; a live authenticated multi-gigabyte transfer was not performed during implementation. Existing SAR processing tests remain separate from real-product scientific acceptance.

Final download checks on October 4, 2026: 24 acquisition tests passed with the live catalogue check enabled; the full native suite passed 109 tests with nine external-service checks skipped; the SAR Python suite passed 21 tests; the retained legacy suite passed 3,899 tests with one skipped. Strict Swift formatting and repository formatting passed. The installed SNAP GPT starts and exposes the required orbit, border/thermal-noise, calibration, speckle and terrain-correction operators. This verifies engine availability, not real-product calibration/geocoding.

## Real-product terrain correction fix — October 4, 2026

A downloaded Sentinel-1D IW GRD acquisition from September 20, 2026 (relative orbit 66) exposed a SNAP 14.0.1 `ArithmeticException: / by zero` in terrain-correction tile initialization. The orbit/noise/calibration stages succeeded independently. The graph now uses SNAP's geographic `WGS84(DD)` projection selector instead of the `EPSG:4326` selector that reproduced this failure. Raster admission still requires the resulting GeoTIFF's actual EPSG:4326 CRS and valid geographic grid; this does not bypass terrain correction or manufacture georeferencing.

An isolated, temporary test processed a small Milan subset (longitude 9.19–9.21, latitude 45.45–45.47), producing matching 112 × 113 Float32 VV/VH Sigma0 GeoTIFFs in EPSG:4326 with 12,656 positive valid pixels per band. The existing output renderer generated VV, VH and composite products successfully. Temporary extraction and raster outputs were removed; the original downloaded ZIP and failed-job diagnostics were retained. This validates the corrected integration on a small real area, not complete-scene performance or independent radiometric/geographic accuracy.

SNAP's precise-orbit request fell back to a restituted orbit for this acquisition. The interface now describes that possibility explicitly. `continueOnFail=false` prevents proceeding when orbit correction fails, but does not prohibit SNAP's internal fallback to restituted orbit data. The diagnostic log records which orbit was used; a checked orbit option must not be interpreted as proof that a precise orbit was available.

## Live worker status delivery fix — October 4, 2026

During a full-scene run, the UI remained at "Queued for SAR processing" even though the persisted worker events showed processing and SNAP was actively writing calibrated GeoTIFFs. Foundation's `FileHandle.read(upToCount: 8192)` buffered the small JSON-line status stream until more bytes arrived or the child exited. The process adapter now uses a bounded blocking POSIX pipe read in its existing detached task; available bytes are parsed immediately, interrupted reads are retried, and existing response limits and cancellation handling remain in place. SNAP's internal processing phase still has indeterminate progress; this fix does not invent a percentage or ETA.

A subprocess regression fixture waits for acknowledgement from a processing callback before it exits. The test failed against the old reader and passed against the corrected reader, confirming status arrives during the job rather than only at completion. All seven native SAR tests passed. An already-running app continues using its loaded adapter until restarted; the active acquisition should be allowed to finish before restarting to pick up this UI fix.

## Dedicated satellite analysis section

SAR search/download and processing/results now live under **Satellite → SAR workflow**. The existing daily imagery controls live under **Reference imagery**. **Infrastructure review** adds saved sites, comparable acquisition pairs, bounded scientific log-ratio change measurement, review decisions and exports. See [the infrastructure workspace guide](SATELLITE-INFRASTRUCTURE-REVIEW.md) for usage, stages, storage and limits. This comparison consumes completed scientific Sigma0 rasters and does not rerun SNAP.

## Identifiable downloads and results

Managed downloads now use the admitted official Sentinel-1 product name with a `.zip` suffix, including acquisition times, orbit/product identifiers and the unique scene suffix. On loading the download library, legacy `product.zip` files are renamed in place inside their existing product-UUID directories. Existing destination files are never overwritten; symbolic links and mismatched directory identities are rejected. No multi-gigabyte archive copy is needed. Metadata-driven saved-processing labels identify satellite, acquisition time, mode and relative orbit even for older results recorded under `product.zip`. Historical result metadata is retained; imports and processing resolve old managed `product.zip` paths through the matching download record. Download rows also show the requested search-region coordinates, distinct from the complete acquisition footprint.
