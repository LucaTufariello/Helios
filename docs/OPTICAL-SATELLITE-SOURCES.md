# Native optical satellite sources

The Satellite section has a **Satellite source** selector. Sentinel-1 opens the existing SAR workflow; Sentinel-2, CBERS-4A WPM and PRISMA use the Reference imagery page. Changing source clears its pending search and selects saved images belonging to that sensor, so a previous sensor's overlay is not mistaken for a new result. All controls and mapping remain native Qt Widgets with RHI rendering.

## Detail, coverage and access

| Source | Available view | Access in Helios | Limits |
| --- | --- | --- | --- |
| Sentinel-2 Collection-1 L2A | Provider true-colour RGB, 10 m source pixels | Search dates/clouds/region and load public COG windows | Enlarging pixels cannot resolve individual small buildings. Cloud cover describes the entire tile. |
| Sentinel-1 GRD | Calibrated radar backscatter | Existing Copernicus search/download, SNAP processing and infrastructure review | Radar is not optical imagery; processing and independent validation requirements remain. |
| CBERS-4A WPM L4 | 2 m grayscale PAN; native 8 m RGB | INPE public catalogue search, anonymous AWS COG windows | Archive is regional, primarily South America. No native 2 m RGB or pansharpening is claimed. Unknown cloud cover stays unknown. |
| PRISMA L2D | 5 m grayscale PAN; 30 m VNIR RGB; selected VNIR/SWIR wavelength | Import an extracted, geocoded `.he5` product obtained through ASI's registered service | Free ASI registration/order required. Direct authenticated ASI ordering/download is not integrated. L1/L2B/L2C, ZIP, other swaths and inconsistent geometry are rejected. No automatic material classification or complete spectral-cube analysis. |

A live all-date INPE search on 2026-10-05 returned **zero CBERS WPM L4 scenes for Celle Ligure**, including the PCA-fused catalogue check. This makes CBERS unsuitable as a promised higher-detail substitute for that particular town. Sentinel-2's approximately 146 × 195 source pixels for the town cannot be made into a trustworthy building-detail image with sharpening or interpolation. PRISMA PAN could offer a modest improvement **if an ASI acquisition covers it**; that coverage has not been verified. Sub-metre building review usually needs commercial satellite data or public regional aerial orthophotos, which must be labelled as aerial imagery and associated with their actual acquisition dates.

## Usage

1. Open **Satellite** (`⌘2`). Select the satellite in the inspector's **Satellite source** menu.
2. Navigate to the site and **Draw optical region** or **Use current view**.
3. For Sentinel-2 or CBERS, choose UTC dates and search. Sentinel-2 exposes a tile-cloud limit. CBERS exposes **Panchromatic grayscale** and **Native RGB**; its cloud metadata can be absent.
4. Select an acquisition and **Load selected optical image**. Only the selected region is rendered. A no-match result means no catalogue coverage within those constraints, not a processing failure.
5. For PRISMA, obtain **L2D** from [ASI](https://www.asi.it/en/earth-science/prisma/), extract it, and choose **Import PRISMA L2D HE5**. Choose PAN, native RGB, or a wavelength from 400–2500 nm; then load. An existing region clips the product; disable **Clip PRISMA to selected region** to view its full footprint.
6. Use saved-image selection, focus and pixel-grid display. The labels show sensor, mode, acquisition, source spacing, requested region and output size. Large views are reduced to 2048 pixels per side; zoom does not generate detail.
7. Export PNG, display GeoTIFF or metadata. CBERS and PRISMA additionally export **selected source values GeoTIFF**. These georeferenced float products retain DN/reflectance separately from stretched display images. They contain the selected bands on the bounded viewer grid, not the entire original acquisition/cube.

## Implementation and dependencies

`SentinelOpticalQuery/Scene/Result` retain their existing names and backward-readable saved Sentinel-2 documents; optional source identity defaults legacy scenes to Sentinel-2. Provider-specific scene validation and fixed asset admission live in `native/desktop/Core/OpticalCatalog.cpp` and `native/workers/Optical/sentinel2.py`. CBERS uses `https://data.inpe.br/bdc/stac/v1/search`, collection `CB4A-WPM-L4-DN-1`, and the deterministic COG mirror under `https://brazil-eosats.s3.us-west-2.amazonaws.com/CBERS4A/WPM/`. INPE scenes can exist before the mirror is populated; unavailable assets fail explicitly. There is no substitution by a quicklook or another sensor.

The Qt Satellite window keeps the existing UUID dataset storage under `~/Library/Application Support/Helios/Satellite/optical/`, process adapter, progress/cancellation, saved-image library and export conventions. Python runs in isolated mode through the owned worker host. `sentinel2.py` dispatches to `optical_sources.py`; no GUI processing engine, browser or local server is added. Existing S2 TCI handling is preserved.

Run `native/workers/setup-raster.sh` to install NumPy, Rasterio/GDAL, Pillow and **h5py** in a dedicated runtime. h5py is needed only for PRISMA. SNAP is needed for Sentinel-1 SAR, not the optical viewers.

### CBERS processing

Only orthorectified L4 WPM is admitted. The public mirror contains tiled GeoTIFFs, including signed 16-bit storage for sensor DN. PAN reads BAND0; RGB reads BAND3/BAND2/BAND1 on the native 8 m grid. Valid positive source values are geocoded with GDAL into EPSG:4326; nodata/invalid values remain masked. Small regions use nearest resampling, large regions average downsampling. A local 2/98 percentile stretch creates the display; float DN remain separate. Values are **not calibrated surface reflectance**, and local display stretches cannot serve as quantitative inter-acquisition comparisons.

### PRISMA processing

Only self-contained L2D HDF-EOS/HE5 is read. Embedded hard-link datasets are required; external links, soft links, virtual datasets, external storage and excessively large compressed chunks are rejected. PAN comes from `PRS_L2D_PCO/Data Fields/Cube`; VNIR/SWIR selected bands come from `PRS_L2D_HCO/Data Fields`. Cube order is rows/bands/columns. RGB selects available valid wavelengths nearest 660/550/480 nm; the spectral mode selects the nearest valid VNIR or SWIR band within 30 nm of the request, preserving the actual wavelength. Provided wavelength flags are respected.

The file supplies WGS84 UTM `Epsg_Code` and product corner **pixel-centre** coordinates. Centre-to-centre spacing is derived from the actual dimensions, checked against nominal 5 m or 30 m, and converted to pixel-edge affine coordinates; inconsistent metadata is rejected. No raw swath is assigned approximate georeferencing. Only selected bands/windows are read in 64-row blocks into a temporary crop, which is removed on completion/failure.

Reflectance conversion is product-specific:

```text
reflectance = L2Scale[Pan/Vnir/Swir]Min
            + DN × (L2Scale[Pan/Vnir/Swir]Max - L2Scale[Pan/Vnir/Swir]Min) / 65535
```

Missing or invalid scale factors fail validation. Zero DN and nonzero L2 pixel-error flags are masked. The float reflectance export is separate from percentile-stretched PNG/RGBA GeoTIFF. Source ID, acquisition, scale formula and selected wavelengths are stored in raster tags and JSON. Clouds/shadows are not automatically masked; this is not a validated spectral/material detector. Independent radiometric/geographic validation with real ASI products remains necessary.

## Verification

Automated tests cover legacy Sentinel-2 documents, real CBERS catalogue metadata, fixed-asset security, unknown cloud, source routing, PRISMA PAN/RGB/SWIR selection, non-default scale factors, centre-coordinate geometry, crop windows, error masks, invalid metadata, external HDF links, constant-value stretches, state transitions, float exports and failure cleanup. `Fixtures/PRS_L2D_GENERATED_20260922102800_0001.he5` is a **20 KiB generated mathematical fixture**, not a satellite acquisition; it must never enter the ordinary user library or be used to validate sensor accuracy.

A real public CBERS acquisition `CBERS_4A_WPM_20261003_198_130_L4` was tested on a small window in Brazil (approximately -41.01/-13.51 to -41.00/-13.50): PAN and native RGB, anonymous block access, geocoded preview/display and float DN export. This demonstrates integration, not independently verified geographic/radiometric accuracy. Real PRISMA delivery and geographic/radiometric accuracy remain unverified without an ASI product.

## Sources and extension points

- [INPE L4 collection metadata](https://data.inpe.br/bdc/stac/v1/collections/CB4A-WPM-L4-DN-1) and [CBERS on AWS registry](https://registry.opendata.aws/cbers/).
- [ASI PRISMA mission and access](https://www.asi.it/en/earth-science/prisma/) and [registration requirement](https://www.asi.it/en/2023/08/the-catalogue-of-prisma-acquisitions-is-now-freely-available-on-geoss-portal/).
- [EnMAP-Box L2D importer](https://enmap-box.readthedocs.io/en/latest/usr_section/usr_manual/processing_algorithms/import_data/import_prisma_l2d_product.html) documents subdatasets, quality matrices and geocoded import; [CNR prismaread](https://github.com/IREA-CNR-MI/prismaread) documents product Min/Max scaling. Our lightweight viewer does not require QGIS/EnMAP-Box.

A new source needs a sensor identity, bounded catalogue/input validation, trusted asset admission, a discrete worker renderer and explicit quantity/resolution/access metadata. Reuse the common output grid, display/source separation, progress and library infrastructure. A future pansharpened view must be separately labelled as fused imagery, preserving native source bands and their resolutions; it must not become the spectral analysis input by default.

## Removing saved imagery and sites

In **Satellite → Reference imagery → Saved optical images**, select an image and
choose **Delete saved optical image…**. Confirm **Move to Trash** to remove its
saved preview, display/scientific GeoTIFFs and processing metadata from the library.
The image folder is moved to the macOS Trash; imported PRISMA source products and
exported copies are preserved. If moving to Trash fails, the image stays listed.
The next saved image from the same source is selected automatically, or the
optical overlay clears when none remain. Deletion is unavailable during loading
or export.

Each **Saved sites** row has a trash button. The selected site's infrastructure
review panel also has **Delete saved site…**. Confirmation removes only the saved
name and boundary. Existing review snapshots, notes and imagery remain available,
and are not cascaded into a delete. Canceling either confirmation changes nothing.
