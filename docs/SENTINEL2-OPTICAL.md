# Native Sentinel-2 optical viewing

Satellite → Reference imagery now requests dated Sentinel-2 Collection-1 Level-2A true-colour images. Catalogue discovery uses [Earth Search](https://element84.com/earth-search/examples/), and image blocks come from the public [Sentinel-2 COG archive](https://registry.opendata.aws/sentinel-2-l2a-cogs/) maintained by Element 84. No login, AWS account, paid Sentinel Hub API, browser view or SNAP invocation is required. Network access and the existing Python Rasterio/GDAL, NumPy and Pillow environment are required.

This native workflow retrieves and displays actual georeferenced optical pixels inside Helios, with dated acquisition metadata and local exports.

## Request and view

1. Open **Satellite → Reference imagery** (`⌘2` opens Satellite).
2. Zoom to the area, select **Draw optical region** and click two opposite corners, or choose **Use current view**. Existing saved infrastructure sites can supply the same shared region: select the site, then return to Reference imagery.
3. Set the UTC date range and maximum **tile-wide** cloud percentage, then **Search Sentinel-2 images**. Searches admit at most a one-year date interval and show up to the 40 newest matching Collection-1 L2A scenes. A product may cover only part of the region. Tighten the date range if more than 40 scenes would match.
4. Select an acquisition by its date, platform and tile-wide cloud percentage. **Load selected optical image** retrieves and geocodes its public true-colour COG window. Work runs in a cancellable local background process; progress is indeterminate while remote GDAL block reads run.
5. The dated image appears over the native Metal globe. **Focus optical image**, **Show Sentinel-2 overlay**, saved-image selection and the panel preview help inspect it. Loading/selecting an optical image clears daily NASA imagery so the layers are not confused. Reference-page imagery is distinct from SAR/review-page overlays.
6. Export an optical PNG, georeferenced **display** GeoTIFF or acquisition metadata. **Show local optical files** opens the local dataset directory. Images remain available after restart.

The existing optical basemap and daily NASA imagery controls remain below Sentinel-2. Choosing daily imagery hides the Sentinel-2 overlay; showing a Sentinel-2 overlay clears daily imagery. Satellite basemaps are undated mosaics and remain context only.

## Data meaning and limitations

This reads the provider's three-band **TCI true-colour image**, representing the visible red, green and blue bands. It is a rendered 8-bit RGB product, **not an export of scientific surface-reflectance bands**, a classification or evidence of infrastructure damage. Clouds and shadows are not automatically masked. Scene cloud metadata describes the entire source tile, not the requested region.

The source TCI spacing is normally 10 m. Source pixel spacing is reported separately from the bounded display image: large regions are downsampled to a maximum 2048 pixels per side. Smaller output pixels or zooming do not create extra physical detail. Only one selected tile is rendered; missing/source-nodata coverage stays transparent and is reported. The requested rectangle is retained separately from the actual intersection bounds. No substitute basemap is labelled Sentinel-2 if a request fails.

Georeferencing comes from the source GeoTIFF's projected CRS and Rasterio/GDAL's transformation to EPSG:4326. This is not an independent check of absolute positional accuracy. For infrastructure corroboration, record the exact acquisition and compare it with appropriate dated references; see [the validation plan](SATELLITE-ANALYSIS-PLAN.md).

## Implementation and storage

- `HeliosCore/SentinelOptical.swift`: bounded query/catalogue parsing, scene admission, trusted asset validation and typed request/result metadata.
- `HeliosCore/SarProcessor.swift`: the existing isolated/cancellable command adapter exposes `renderOptical` without putting command syntax in the UI.
- `HeliosMac/OpticalWorkspace.swift`: native search/load state, saved images, interrupted-job recovery, preview controls and atomic exports.
- `HeliosMac/Resources/Optical/sentinel2.py`: request validation, bounded COG window rendering, alpha/nodata handling, geocoding and exports.
- Satellite views, shared region state and the existing Metal raster overlay are reused. SAR calibration/comparison algorithms are unchanged.

```text
~/Library/Application Support/Helios/Satellite/optical/<UUID>/
  result.json          acquisition, requested/actual bounds, date, platform,
                       cloud metadata, CRS, dimensions, coverage and limitations
  preview.png          bounded RGBA map preview
  true-color.tif       georeferenced RGBA display GeoTIFF, not reflectance
  events.jsonl         processing states, timestamps and elapsed times
  diagnostics.log      bounded failure details, when needed
```

Catalogue requests use a fixed HTTPS host with an 8 MiB cap and no redirects. Image asset admission allows only the known public Collection-1 S3 host, scene-specific TCI path, HTTPS and no credentials/query/fragment/traversal. GDAL admits the GTiff driver only, avoiding remote VRT references. GDAL opens COG ranges/overviews with bounded caches/warp memory and a 60-second timeout per HTTP request; target image arrays are capped at 2048 × 2048. Full SAFE extraction/copies are unnecessary. These limits bound normal processing but are not a strict total-process/network-byte guarantee.

Temporary UUID request files are removed after completion/failure/cancellation. Partial outputs are removed by the worker on failure/cancellation; bounded diagnostics remain. Recovery removes interrupted images only when the saved worker PID is confirmed gone. Existing live workers and prior completed images are preserved. Raster export uses atomic staged replacement; metadata export omits the local worker PID.

## Checks

Run `native/macos/test.sh`, `native/macos/test-sar.sh` and `make format-check`. New native tests cover UTC/cloud queries, real public catalogue metadata, URL/size admission, typed result provenance and the Swift-to-Python failure/cleanup boundary. Generated RGB tests cover source channel order, valid CRS/bounds, nodata transparency, region clipping, large-source downsampling, invalid/no-coverage rejection, request bounds and failure cleanup. Fixtures are testing inputs, not fabricated operational acquisitions.

A real live request on October 4 retrieved `S2C_T32TNR_20260927T102053_L2A` over central Milan, rendered it using its actual projected source CRS, and exported EPSG:4326 RGBA with the recorded acquisition date. Live service availability is separate from deterministic test results.

Native UI verification also completed a city-sized 1014 × 1303 request in approximately 19 seconds, a smaller drawn-region request, layer hide/show, separation from SAR/review pages, daily-imagery switching, native PNG/GeoTIFF/metadata exports and saved-image reload after restart. Exported raster bytes, CRS/bounds, acquisition provenance and temporary-file cleanup were checked. A timed-out request was correctly reported and its partial outputs removed; the per-request timeout was increased from 20 to 60 seconds. These workflow checks do not certify sensor or absolute geographic accuracy.

## Small-region viewing

Sentinel-2 visible bands have approximately 10 m spatial resolution. A 1.5–2 km request may contain only about 150–200 pixels per side; fitting this onto a large Retina map magnifies each pixel. This cannot establish small-building or vehicle detail. Pixel-grid display defaults to nearest-neighbour sampling and full opacity, with optional smooth map sampling; an enlargement notice makes the limitation explicit. New native-spacing requests use nearest-neighbour geocoding to avoid a second smoothing step; downsampled regions use area averaging. Reload an older acquisition to replace its earlier bilinear-rendered preview. Scientific spatial detail is never fabricated through sharpening or enlargement. Choose **Show detailed basemap context** for finer visual context from Esri, with its varying, unspecified acquisition dates.

Additional sources and their access/resolution constraints are described in [Native optical satellite sources](OPTICAL-SATELLITE-SOURCES.md). Sentinel-2 remains the default optical source; a Satellite source menu routes radar to its existing workflow.

## Interrupted or slow archive transfers

The public image archive supplies compressed TIFF tiles through HTTP range requests.
Even a small area can require a multi-megabyte source tile. A catalogue search can
succeed while the image transfer is slow or interrupted; this does not by itself
indicate a missing Python installation.

Remote Sentinel-2 and CBERS reads now allow an active transfer up to 600 seconds per
HTTP request, with a 15-second connection timeout and a low-speed threshold of
1 KiB/s over 20 seconds. Read failures have at most three application attempts;
subsequent attempts reopen the admitted asset with fresh cached state and serial
HTTP/1.1 transfers. HTTP-level retries remain bounded as well. These limits are per
request, not an overall job duration; regions requiring several tiles can take
longer. See [GDAL HTTP options](https://gdal.org/en/stable/user/configoptions.html)
and [remote-file cache behavior](https://gdal.org/en/stable/user/virtual_file_systems.html).

Retries keep the exact acquisition and region. They never replace failed imagery
with another date, a basemap or invented pixels. Product validation, cancellation,
known unavailable assets and export failures are distinguished from retryable
remote reads. Progress reports retry attempts, and **Show diagnostic log in Finder**
is available after a worker failure. The log keeps at most 16,000 characters,
including retry failures when a later attempt succeeds.

**Cancel optical request** first asks the worker to stop. If it remains blocked in
native raster/network code, Helios force-stops that optical worker after three
seconds and removes its known partial outputs. Existing saved imagery and source
products are retained. This optical-only fallback does not change SNAP/GPT process
group cancellation or SAR scientific processing.

Regression tests include a real local HTTP server returning a truncated TIFF body,
fresh-cache recovery, bounded retry exhaustion, unavailable assets, export errors,
backoff cancellation and an unresponsive worker with partial-output cleanup.

Verification on 12 October 2026: the user's failing acquisition
`S2A_T33TVL_20261003T100349_L2A` and original region were reproduced with the old
60-second limit (incomplete tile), then successfully rendered with the fix in
486.6 seconds on the observed slow archive connection. The 265 × 261 RGBA preview
matched the EPSG:4326 GeoTIFF exactly, with 100% non-nodata coverage. This verifies
transfer/rendering, not cloud-free coverage or independent sensor geolocation.
The native suite passed 167 tests (10 optional live tests skipped); all 62 Python
SAR/optical tests passed. The arm64 release build and signature verification passed.
Local reports are in `output/qa/optical-recovery-2026-10-12`.
