import * as Cesium from 'cesium';
import {
  GIBS_LAYERS,
  GIBS_MAX_LEVEL,
  gibsDateString,
  gibsUrlTemplate,
} from '../data/gibsLayers.js';

export {
  GIBS_BASE_URL,
  GIBS_TILE_MATRIX_SET,
  GIBS_MAX_LEVEL,
  GIBS_LAYERS,
  gibsDateString,
  gibsUrlTemplate,
} from '../data/gibsLayers.js';

/**
 * NASA GIBS daily imagery (free, keyless WMTS).
 *
 * The only truly live global satellite imagery that is free + keyless.
 * Resolution is ~250m (VIIRS) — neighborhoods, not cars — and `best`
 * lags ~1 day, so the default date is yesterday (UTC).
 *
 * See DATA_SOURCES.md (NASA GIBS/Worldview entry).
 */

export const GIBS_ATTRIBUTION_HTML =
  'Sat Fresh: <a href="https://worldview.earthdata.nasa.gov" target="_blank" rel="noopener">NASA Worldview / GIBS</a> (VIIRS TrueColor ~250m, yesterday)';

/**
 * Build a keyless Cesium imagery provider for one GIBS layer.
 * @param {string} layerKey - Key of GIBS_LAYERS (default 'trueColor').
 * @param {string|null} dateStr - YYYY-MM-DD; defaults to yesterday (UTC).
 */
export function createGibsImageryProvider(
  layerKey = 'trueColor',
  dateStr = null,
) {
  const layer = GIBS_LAYERS[layerKey] || GIBS_LAYERS.trueColor;
  const date = dateStr || gibsDateString(new Date(), 1);
  return new Cesium.UrlTemplateImageryProvider({
    url: gibsUrlTemplate(layer.id, date, layer.format),
    tilingScheme: new Cesium.WebMercatorTilingScheme(),
    maximumLevel: GIBS_MAX_LEVEL,
    minimumLevel: 0,
    credit: new Cesium.Credit(GIBS_ATTRIBUTION_HTML, false),
  });
}
