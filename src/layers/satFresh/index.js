import * as Cesium from 'cesium';
import {
  GIBS_LAYERS,
  GIBS_ATTRIBUTION_HTML,
  createGibsImageryProvider,
  gibsDateString,
} from '../../maps/gibs.js';
import { registerDynamicCredit } from '../../data/dataCredits.js';
import {
  isNight,
  subsolarPoint,
  terminatorPositions,
} from '../../data/terminator.js';

export { createSatFreshSource } from './source.js';

export const SAT_FRESH_CREDIT = {
  key: 'nasa-gibs',
  html:
    'Sat Fresh (free): ' +
    '<a href="https://worldview.earthdata.nasa.gov" target="_blank" rel="noopener">NASA Worldview / GIBS</a> ' +
    '(VIIRS ~250m, yesterday — neighborhoods, not cars)',
};

const TERMINATOR_ENTITY_ID = 'sat-fresh-terminator';
const BASEMAP_DISCLAIMER =
  'Basemap = daytime mosaic (months/years old) — not live. Use Sat Fresh for yesterday.';

/**
 * Sat Fresh overlay: keyless NASA GIBS daily imagery + solar terminator.
 *
 * - GIBS TrueColor (yesterday) as an overlay ImageryLayer (index > 0, so the
 *   base map stack is untouched).
 * - Night side suggestion: when the camera target is in darkness, the layer
 *   stats name the Black Marble night-lights layer instead of a darkened
 *   basemap shader.
 * - Terminator polyline (great-circle day/night line) refreshed per minute.
 */
export function createSatFreshLayer({ source } = {}) {
  let _viewer = null;
  let _imageryLayer = null;
  let _terminator = null;
  let _enabled = false;
  let _opacity = 0.85;
  let _mode = 'day';
  let _date = null;
  let _lastUpdate = null;
  let _refreshTimer = null;

  const layer = {
    id: 'sat-fresh',
    name: 'Sat Fresh (free)',
    icon: '🛰️',
    source: 'NASA GIBS · keyless daily',
    showInTogglePanel: true,
    updateInterval: 86_400_000,

    init(viewer) {
      if (_viewer) throw new Error('Sat Fresh layer is already initialized');
      _viewer = viewer;
    },

    enable() {
      _enabled = true;
      const snapshot = source?.getSnapshot?.() || null;
      _date = snapshot?.date || gibsDateString(new Date(), 1);
      _attachImagery();
      _attachTerminator();
      if (_viewer) registerDynamicCredit(_viewer, SAT_FRESH_CREDIT);
      _lastUpdate = Date.now();
      _refreshTimer = setInterval(() => {
        if (_enabled) _refreshTerminator();
      }, 60_000);
      _refreshTimer?.unref?.();
    },

    disable() {
      _enabled = false;
      if (_refreshTimer) clearInterval(_refreshTimer);
      _refreshTimer = null;
      _detachImagery();
      _detachTerminator();
    },

    async update() {
      if (!_enabled) return false;
      // Daily cadence: re-resolve the date; a new day swaps the provider.
      const snapshot = source?.getSnapshot?.() || null;
      const nextDate = snapshot?.date || gibsDateString(new Date(), 1);
      if (nextDate !== _date) {
        _date = nextDate;
        _attachImagery();
      }
      _refreshTerminator();
      _lastUpdate = Date.now();
      return true;
    },

    setOpacity(value) {
      const numeric = Number(value);
      _opacity = Number.isFinite(numeric)
        ? Math.max(0, Math.min(1, numeric))
        : 0.85;
      if (_imageryLayer) _imageryLayer.alpha = _opacity;
    },

    setMode(mode) {
      if (mode === 'day' || mode === 'night' || mode === 'fires') _mode = mode;
      if (_enabled) _attachImagery();
    },

    getStats() {
      const center = _cameraGroundCenter();
      const night = center ? isNight(center.lat, center.lon, new Date()) : null;
      return {
        count: _imageryLayer ? 1 : 0,
        countLabel: _date || undefined,
        lastUpdate: _lastUpdate,
        source: 'NASA GIBS · keyless daily',
        coverage:
          '250m — neighborhoods, not cars. Not live video. ' +
          BASEMAP_DISCLAIMER,
        night,
        suggestedLayer:
          night === true
            ? GIBS_LAYERS.nightLights.id
            : GIBS_LAYERS.trueColor.id,
        terminatorAt: subsolarPoint(new Date()),
      };
    },

    getBasemapDisclaimer() {
      return BASEMAP_DISCLAIMER;
    },

    destroy() {
      if (_refreshTimer) clearInterval(_refreshTimer);
      _refreshTimer = null;
      _detachImagery();
      _detachTerminator();
      _viewer = null;
      _enabled = false;
    },
  };

  function _attachImagery() {
    if (!_viewer) return;
    _detachImagery();
    const layerKey =
      _mode === 'night'
        ? 'nightLights'
        : _mode === 'fires'
          ? 'thermalAnomalies'
          : 'trueColor';
    try {
      const provider = createGibsImageryProvider(layerKey, _date);
      _imageryLayer = _viewer.imageryLayers.addImageryProvider(provider);
      _imageryLayer.alpha = _opacity;
      _imageryLayer.show = true;
    } catch (error) {
      console.warn(
        '[Data:SatFresh] GIBS provider failed:',
        error?.message || error,
      );
      _imageryLayer = null;
    }
  }

  function _detachImagery() {
    if (_viewer && _imageryLayer) {
      try {
        _viewer.imageryLayers.remove(_imageryLayer, true);
      } catch {
        /* best effort */
      }
    }
    _imageryLayer = null;
  }

  function _attachTerminator() {
    if (!_viewer) return;
    _detachTerminator();
    const positions = terminatorPositions(new Date(), 2).map(({ lat, lon }) =>
      Cesium.Cartesian3.fromDegrees(lon, lat),
    );
    if (!positions.length) return;
    _terminator = _viewer.entities.add(
      new Cesium.Entity({
        id: TERMINATOR_ENTITY_ID,
        name: 'Solar terminator (day/night line)',
        polyline: {
          positions,
          width: 2,
          material: new Cesium.ColorMaterialProperty(
            Cesium.Color.YELLOW.withAlpha(0.7),
          ),
          clampToGround: true,
        },
      }),
    );
  }

  function _refreshTerminator() {
    if (!_viewer || !_terminator) return;
    const positions = terminatorPositions(new Date(), 2).map(({ lat, lon }) =>
      Cesium.Cartesian3.fromDegrees(lon, lat),
    );
    if (positions.length) _terminator.polyline.positions = positions;
  }

  function _detachTerminator() {
    if (_viewer && _terminator) {
      try {
        _viewer.entities.remove(_terminator);
      } catch {
        /* best effort */
      }
    }
    _terminator = null;
  }

  function _cameraGroundCenter() {
    try {
      const scene = _viewer?.scene;
      const camera = _viewer?.camera;
      if (!scene || !camera) return null;
      const ray = camera.getPickRay?.(
        new Cesium.Cartesian2(
          scene.canvas.clientWidth / 2,
          scene.canvas.clientHeight / 2,
        ),
      );
      const cartesian = scene.globe.pick?.(ray, scene);
      if (!cartesian) return null;
      const carto = Cesium.Cartographic.fromCartesian(cartesian);
      if (!carto) return null;
      return {
        lat: Cesium.Math.toDegrees(carto.latitude),
        lon: Cesium.Math.toDegrees(carto.longitude),
      };
    } catch {
      return null;
    }
  }

  return layer;
}

export { GIBS_ATTRIBUTION_HTML };
