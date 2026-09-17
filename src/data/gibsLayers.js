/**
 * NASA GIBS layer descriptors + date/URL helpers (portable).
 *
 * No Cesium, no browser globals beyond Date — safe for `source.js` imports.
 * The Cesium provider factory lives in `src/maps/gibs.js`.
 */

export const GIBS_BASE_URL =
  'https://gibs.earthdata.nasa.gov/wmts/epsg3857/best';
export const GIBS_TILE_MATRIX_SET = 'GoogleMapsCompatible_Level9';
export const GIBS_MAX_LEVEL = 9;

export const GIBS_LAYERS = Object.freeze({
  trueColor: Object.freeze({
    id: 'VIIRS_SNPP_CorrectedReflectance_TrueColor',
    title: 'TrueColor (day)',
    format: 'jpg',
    resolution: '250m — neighborhoods, not cars',
  }),
  terraFallback: Object.freeze({
    id: 'MODIS_Terra_CorrectedReflectance_TrueColor',
    title: 'Terra TrueColor (fallback)',
    format: 'jpg',
    resolution: '250m — neighborhoods, not cars',
  }),
  nightLights: Object.freeze({
    id: 'VIIRS_SNPP_DayNightBand_ENCC',
    title: 'Black Marble (night lights)',
    format: 'png',
    resolution: '500m — city lights, not streets',
  }),
  thermalAnomalies: Object.freeze({
    id: 'MODIS_Terra_Thermal_Anomalies_All',
    title: 'Thermal anomalies (fires)',
    format: 'png',
    resolution: '1km — same detections as FIRMS, as raster',
  }),
});

/** YYYY-MM-DD for `daysAgo` days before `at` (UTC). GIBS `best` lags ~1 day. */
export function gibsDateString(at = new Date(), daysAgo = 1) {
  const base = at instanceof Date ? at : new Date(at);
  const shifted = new Date(base.getTime() - daysAgo * 86_400_000);
  return shifted.toISOString().slice(0, 10);
}

/** WMTS REST tile URL template for one GIBS layer + date (XYZ placeholders). */
export function gibsUrlTemplate(layerId, dateStr, format = 'jpg') {
  return (
    `${GIBS_BASE_URL}/${layerId}/default/${dateStr}/` +
    `${GIBS_TILE_MATRIX_SET}/{z}/{y}/{x}.${format}`
  );
}
