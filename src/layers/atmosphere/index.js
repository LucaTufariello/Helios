import * as Cesium from 'cesium';
import { registerDynamicCredit } from '../../data/dataCredits.js';
import {
  atmosphereDetailsFromSnapshot,
  atmosphereSectionHtml,
} from './panel.js';

export { createAtmosphereSource } from './source.js';

export const ATMOSPHERE_CREDIT = {
  key: 'open-meteo',
  html:
    'Atmosphere (free): ' +
    '<a href="https://open-meteo.com/" target="_blank" rel="noopener">Open-Meteo</a> ' +
    '(CC BY 4.0 — model readout, not live video)',
};

/**
 * Atmosphere layer: current conditions + today/tomorrow + European AQI for
 * the camera point, via the keyless Open-Meteo proxy.
 *
 * A data readout, not a scene overlay: no entities are drawn. The value is
 * the Context-panel section (condition, wind, rain odds, EAQI band) that
 * refreshes with the camera.
 */
export function createAtmosphereLayer({ source, overlayHost } = {}) {
  if (typeof source?.getSnapshot !== 'function')
    throw new TypeError('Atmosphere layer requires a snapshot source');
  let _viewer = null;
  let _enabled = false;
  let _request = null;
  let _last = null;
  let _lastUpdate = null;
  let _lastError = null;
  let _section = null;
  let _sectionHtml = null;

  /** Plain details for the Context-panel section (and tests). */
  function getDetails() {
    return atmosphereDetailsFromSnapshot(_last, {
      updatedAt: _lastUpdate,
      error: _lastError,
    });
  }

  /**
   * Create (or re-acquire) the Context-panel section node. The host lives in
   * the static context template; without a document (Node tests) this is a
   * no-op and the layer still works.
   */
  function ensureSection() {
    if (_section || typeof document === 'undefined') return;
    const host =
      document.getElementById('context-atmosphere-host') ||
      document.getElementById('right-context-rail');
    if (!host) return;
    _section = document.createElement('section');
    _section.id = 'context-atmosphere-view';
    _section.className = 'context-mode-view context-atmosphere-view';
    _section.setAttribute('aria-label', 'Atmosphere readout');
    host.appendChild(_section);
  }

  /** Re-render the section; skips DOM writes when nothing changed. */
  function renderSection() {
    if (typeof document === 'undefined') return;
    ensureSection();
    if (!_section) return;
    const html = atmosphereSectionHtml(getDetails());
    if (html === _sectionHtml) return;
    _sectionHtml = html;
    _section.innerHTML = html;
  }

  function removeSection() {
    _sectionHtml = null;
    if (_section) {
      try {
        _section.remove();
      } catch {
        /* best effort */
      }
    }
    _section = null;
  }

  const layer = {
    id: 'atmosphere',
    name: 'Atmosphere (free)',
    icon: '🌤️',
    source: 'Open-Meteo · keyless, CC BY 4.0',
    showInTogglePanel: true,
    updateInterval: 900_000,

    init(viewer) {
      if (_viewer) throw new Error('Atmosphere layer is already initialized');
      _viewer = viewer;
    },

    enable() {
      _enabled = true;
      if (_viewer) registerDynamicCredit(_viewer, ATMOSPHERE_CREDIT);
      renderSection();
      void layer.refresh();
    },

    disable() {
      _enabled = false;
      _request?.abort();
      _request = null;
      removeSection();
      overlayHost?.clearSource?.('atmosphere');
      overlayHost?.setVisible?.('atmosphere', false);
    },

    async refresh(viewport, signal) {
      if (!_enabled || !_viewer) return true;
      // The manager calls update(viewer, { signal }) — a viewport is only
      // ever passed explicitly (tests / direct callers). Never treat the
      // manager's options object as coordinates.
      const center =
        viewport &&
        Number.isFinite(viewport.lat) &&
        Number.isFinite(viewport.lon)
          ? viewport
          : _cameraGroundCenter();
      // No ground under the camera (sky/horizon view): stay enabled with
      // nothing to query yet; the next update retries.
      if (!center) return true;
      _request?.abort();
      const request = new AbortController();
      _request = request;
      const onExternalAbort = () => request.abort();
      signal?.addEventListener?.('abort', onExternalAbort, { once: true });
      try {
        const snapshot = await source.getSnapshot({
          lat: center.lat,
          lon: center.lon,
          signal: request.signal,
        });
        if (request.signal.aborted || _request !== request || !_enabled)
          return false;
        _last = snapshot;
        _lastUpdate = Date.now();
        _lastError = null;
        renderSection();
        return true;
      } catch (error) {
        if (request.signal.aborted || _request !== request || !_enabled)
          return false;
        _lastError = error?.message || 'Atmosphere source unavailable';
        renderSection();
        return false;
      } finally {
        signal?.removeEventListener?.('abort', onExternalAbort);
        if (_request === request) _request = null;
      }
    },

    /**
     * Manager-owned refresh contract: update(viewer, { signal }).
     * The second argument is lifecycle options, never a viewport — the
     * viewport is always derived from the camera here.
     */
    async update(viewer, options) {
      return layer.refresh(undefined, options?.signal);
    },

    getAnalystRecords() {
      if (!_last?.item) return [];
      return [
        {
          temperatureC: _last.item.temperatureC,
          weatherLabel: _last.item.weatherLabel,
          europeanAqi: _last.item.europeanAqi,
          lat: _last.lat,
          lon: _last.lon,
        },
      ];
    },

    getDetails() {
      return getDetails();
    },

    getStats() {
      const item = _last?.item || null;
      return {
        count: item ? 1 : 0,
        countLabel:
          item?.temperatureC != null ? `${item.temperatureC}°C` : undefined,
        lastUpdate: _lastUpdate,
        error: _lastError,
        source: 'Open-Meteo · keyless, CC BY 4.0',
        coverage: item
          ? `${item.weatherLabel || 'Unknown'} · ${item.temperatureC ?? '?'}°C` +
            (Number.isFinite(item?.europeanAqi)
              ? ` · EAQI ${item.europeanAqi}`
              : '')
          : 'No readout yet — enable over land.',
      };
    },

    destroy() {
      _request?.abort();
      _request = null;
      _enabled = false;
      removeSection();
      overlayHost?.clearSource?.('atmosphere');
      _viewer = null;
    },
  };

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
