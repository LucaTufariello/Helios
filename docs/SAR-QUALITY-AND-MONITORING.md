# SAR quality checks and monitoring direction

The current product is calibrated Sigma0, geometrically terrain-corrected with SNAP, with Float32 scientific values retained. It is not yet an independently validated absolute radiometric/geolocation product. No automated change detection or classification is added by this quality-check tooling.

## Reproducible developer checks

`native/macos/Validation/sar_quality.py` is separate from the production raster calculations. It reuses safe product admission and SNAP process control, while independently calculating the expected dB values and calibration LUT values. It reads saved rasters in blocks rather than loading an acquisition into memory. Original downloads, completed outputs, and existing job records are read only. Optional calibration probes use a temporary extraction that is removed after success or failure.

From the repository root, with the existing configured SAR Python environment:

```sh
"$HOME/Library/Application Support/Helios/SAR/runtime/bin/python3" -B \
  native/macos/Validation/sar_quality.py \
  --job "$HOME/Library/Application Support/Helios/SAR/jobs/<completed-job-UUID>" \
  --report output/validation/quality.json \
  --gpt /Applications/esa-snap/bin/gpt
native/macos/test-sar.sh
```

Omit `--gpt` to run the export/grid checks without extracting data or invoking SNAP. The JSON report distinguishes checks performed from remaining independent-reference checks. Exit status is nonzero when a completed numerical check fails; a successful exit does not certify absolute accuracy.

### Results from the September 20, 2026 Sentinel-1D acquisition

The completed job `D8E30D8D-23E7-4642-989C-52CED875CE17` was checked locally on October 4, 2026. Its generated report is `output/validation/sentinel1-20260920-quality.json` (local artifact, not a committed satellite dataset).

- VV and VH scientific rasters and their dB exports share the same EPSG:4326 grid. Recorded overlay bounds agree with actual GeoTIFF bounds. This checks exported coordinate consistency, not geographic accuracy against ground control.
- Across all positive valid output pixels, the independently calculated `10 × log10(Sigma0)` agreed with the dB exports: 152,124,386 pixels for VV, maximum error below 0.000005 dB; 151,541,697 pixels for VH, maximum error below 0.000007 dB. Invalid dB masks had zero mismatches. These are Float32 numerical tolerances, not measurement uncertainty.
- At three distributed source positions for each polarization, an independent `DN² / interpolated_sigmaLUT²` calculation agreed with a separate SNAP calibration-only probe to less than 0.000001 dB. The LUT uses range interpolation and acquisition/vector azimuth times. This isolates the calibration stage; it does not validate noise correction, terrain correction, filtering, or resampling. Six samples are preliminary evidence, not exhaustive calibration verification.
- The SAFE manifest footprint envelope is retained as context. The polygon's four-corner envelope and the terrain-corrected raster rectangle need not match exactly; curvature, DEM correction, and nodata padding prevent treating their difference as a measured geolocation error.
- The acquisition used restituted orbit data, as recorded in the processing diagnostic. Its requested precise-orbit option must not be read as confirmation of a precise orbit.

## Independent validation still required

1. **Geographic alignment:** obtain independent reference data with recorded CRS, acquisition/source date and accuracy. Use identifiable stable ground features in several flat and mountainous areas. Record matched SAR/reference coordinates, residual east/north errors in metres, sample count, robust spread and outliers. Separate ground position from SAR layover and surface-height effects. The Esri display mosaic and a visual impression of alignment are not survey-grade ground control.
2. **External processor comparison:** obtain independently processed data for the same acquisition, with compatible radiometry, noise handling, orbit, DEM, pixel grid, resampling and masks. Compare region statistics and residuals in dB on common valid pixels. Re-running the same SNAP graph through its GUI is a repeatability check, not an independent algorithmic reference.
3. **Absolute radiometry:** use published calibration/reference measurements with known uncertainty. Sentinel-1 mission performance specifications are not a measured accuracy result for Helios. A comparison against another processor alone cannot establish absolute sensor calibration.
4. **Coverage:** repeat checks across additional scenes, satellites, low-backscatter regions, coastlines and terrain types. Report nodata, negative noise-corrected power, and rejected pixels explicitly. Do not fill missing pixels merely to produce a complete-looking map.

ASF's [independent RTC processing guide](https://hyp3-docs.asf.alaska.edu/guides/rtc_product_guide/) describes GAMMA-based processing, with terrain-area normalization and gamma0 by default. Those products cannot be assumed pixel-equivalent to Helios's current unflattened Sigma0 just because both are georeferenced. Match the processing quantities and terrain normalization, or document and explain the expected differences. The provider's processing engine and access conditions are separate from Helios's open-source application.

## Recommended application: infrastructure and site change monitoring

The product priority is infrastructure and site change monitoring: industrial facilities, construction sites, transport terminals, ports and energy sites. The initial [native infrastructure review workflow](SATELLITE-INFRASTRUCTURE-REVIEW.md) now provides a saved area of interest with dated compatible acquisitions, before/after viewing, and reviewable radar-change candidates. It measures where a site's radar response changed, while retaining evidence and uncertainty; a physical cause needs corroboration. Start with large changes such as earthworks, expanded paved areas and major construction footprints; the current SAR product cannot reliably classify individual vehicles or determine structural damage or operating status.

The first validation demonstrator should also reflect this priority: a documented infrastructure construction/expansion area with independently dated reference imagery and nearby stable control areas. Measure registration errors and false changes in the control areas, then evaluate candidate changes against documented site changes. A radar change alone does not establish that construction occurred. Flood/water mapping remains an optional later application, rather than the feature driving this roadmap.

Sentinel-1 IW high-resolution GRD has approximately 20 × 23 m spatial resolution, distinct from its pixel spacing ([product specifications](https://sentiwiki.copernicus.eu/web/s1-products)). The globe's bounded preview is coarser still. Analysis must use the retained scientific rasters, and detailed interpretation should be corroborated with dated optical/reference imagery. Dated Sentinel-2 Level-2A true-colour viewing is available in Satellite → Reference imagery ([guide](SENTINEL2-OPTICAL.md)). Automated optical comparison and reflectance analysis remain future work; the Esri display mosaic alone does not provide dated evidence.

Further validation and refinement order:

1. Save an area, source acquisition identifiers, time range, processing options and review notes. Process a cropped area to limit disk/runtime cost, while retaining the original acquisition and valid geocoding.
2. Select acquisitions with compatible mode, polarization, relative orbit and pass direction. Reproject/resample onto a shared analysis grid, verify co-registration, and use a common validity mask. Record satellite-specific calibration differences rather than assuming interchangeability.
3. Compute `delta_dB = after_dB - before_dB` from scientific values. Compare consistent scales rather than independently stretched RGB previews. Estimate variability from stable areas or a time-series baseline; suppress isolated speckle responses and quantify minimum spatial support.
4. Present increased/decreased backscatter regions within each saved site, their area, timestamps, measured changes and invalid-pixel coverage. A candidate is a change in radar response, not proof of a new building or particular activity. Weather, moisture, vegetation, season and viewing geometry can change the signal.
5. Review candidates alongside optical/reference layers and contextual records; save accepted/rejected findings and portable evidence. Add cause-specific detectors only after evaluating against labeled reference examples.

For mountainous or mixed-orbit comparisons, add explicit radiometric terrain flattening and appropriate geometry/layover/shadow quality information before treating slope-related differences as changes. Preserve the current Sigma0 workflow as a clearly labeled product. ASF documents [log-ratio change detection](https://hyp3-docs.asf.alaska.edu/guides/rtc_product_guide/#quantifying-change-over-time) and the [geocoding guide](https://sentinels.copernicus.eu/documents/247904/1653442/Guide-to-Sentinel-1-Geocoding.pdf) explains positional-error sources.

This work establishes a first reproducible quality baseline. The initial analytical review workspace is implemented, with bounded local-mean Sigma0 log ratios and human decisions. Sub-pixel registration, stable-area/time-series variability models, cause-specific detectors and independent geographic/radiometric validation remain future work. No Maven feature parity or validated infrastructure-event classification is claimed.
