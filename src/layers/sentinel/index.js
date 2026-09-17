import * as Cesium from 'cesium';
import { registerDynamicCredit } from '../../data/dataCredits.js';
import { sentinelDetailsFromSnapshot, sentinelSectionHtml } from './panel.js';

export { createSentinelSource, sentinelBrowserUrls } from './source.js';

export const SENTINEL_CREDIT = {
  key: 'copernicus-sentinel',
  html:
    'Sentinel latest-clear (free): ' +
    '<a href="https://dataspace.copernicus.eu/" target="_blank" rel="noopener">Copernicus Sentinel, ESA/EU</a> ' +
    '(10m S2 optical / S1 SAR preview — days old, not live)',
};

/**
 * Sentinel free layer: latest-clear S2 (10m) + S1 SAR preview metadata.
 *
 * Without CDSE creds the proxy still returns public STAC search results, so
 * this layer degrades to a viewport footprint + date/cloud% + browser
 * link-outs rather than erroring. S1 renders as a grayscale footprint
 * labeled "radar, not photo".
 */
export function createSentinelLayer({ source, overlayHost } = {}) {
  if (typeof source?.getSnapshot !== 'function')
    throw new TypeError('Sentinel layer requires a snapshot source');
  let _viewer = null;
  let _dataSource = null;
  let _enabled = false;
  let _request = null;
  let _last = null;
  let _lastUpdate = null;
  let _lastError = null;
  let _kind = 's2';
  let _sentinelSection = null;
  let _sentinelSectionHtml = null;

  /** Plain details for the Context-panel section (and tests). */
  function getDetails() {
    return sentinelDetailsFromSnapshot(_last, _kind, {
      updatedAt: _lastUpdate,
      error: _lastError,
    });
  }

  /**
   * Create (or re-acquire) the Context-panel section node. The host lives in
   * the static context template; without a document (Node tests) this is a
   * no-op and the layer still works.
   */
  function ensureSentinelSection() {
    if (_sentinelSection || typeof document === 'undefined') return;
    const host =
      document.getElementById('context-sentinel-host') ||
      document.getElementById('right-context-rail');
    if (!host) return;
    _sentinelSection = document.createElement('section');
    _sentinelSection.id = 'context-sentinel-view';
    _sentinelSection.className = 'context-mode-view context-sentinel-view';
    _sentinelSection.setAttribute('aria-label', 'Sentinel latest-clear');
    host.appendChild(_sentinelSection);
  }

  /** Re-render the section; skips DOM writes when nothing changed. */
  function renderSentinelSection() {
    if (typeof document === 'undefined') return;
    ensureSentinelSection();
    if (!_sentinelSection) return;
    const html = sentinelSectionHtml(getDetails());
    if (html === _sentinelSectionHtml) return;
    _sentinelSectionHtml = html;
    _sentinelSection.innerHTML = html;
  }

  function removeSentinelSection() {
    _sentinelSectionHtml = null;
    if (_sentinelSection) {
      try {
        _sentinelSection.remove();
      } catch {
        /* best effort */
      }
    }
    _sentinelSection = null;
  }

  const layer = {
    id: 'sentinel-free',
    name: 'Sentinel Latest-Clear (free)',
    icon: '🔭',
    source: 'Copernicus Sentinel · free, no key',
    showInTogglePanel: true,
    updateInterval: 3_600_000,

    init(viewer) {
      if (_viewer) throw new Error('Sentinel layer is already initialized');
      _viewer = viewer;
      _dataSource = new Cesium.CustomDataSource('sentinel-free');
      _dataSource.show = false;
      viewer.dataSources.add(_dataSource);
    },

    enable() {
      _enabled = true;
      if (_dataSource) _dataSource.show = true;
      if (_viewer) registerDynamicCredit(_viewer, SENTINEL_CREDIT);
      renderSentinelSection();
      void layer.refresh();
    },

    disable() {
      _enabled = false;
      _request?.abort();
      _request = null;
      removeSentinelSection();
      if (_dataSource) {
        _dataSource.entities.removeAll();
        _dataSource.show = false;
      }
      overlayHost?.clearSource?.('sentinel-free');
      overlayHost?.setVisible?.('sentinel-free', false);
    },

    setKind(kind) {
      if (kind === 's1' || kind === 's2') {
        _kind = kind;
        renderSentinelSection();
        if (_enabled) void layer.refresh();
      }
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
          kind: _kind,
          signal: request.signal,
        });
        if (request.signal.aborted || _request !== request || !_enabled)
          return false;
        _renderSnapshot(snapshot, center);
        _last = snapshot;
        _lastUpdate = Date.now();
        _lastError = null;
        renderSentinelSection();
        return true;
      } catch (error) {
        if (request.signal.aborted || _request !== request || !_enabled)
          return false;
        _lastError = error?.message || 'Sentinel source unavailable';
        renderSentinelSection();
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
          kind: _last.kind,
          datetime: _last.item.datetime,
          cloudCover: _last.item.cloudCover,
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
      const fallbackLinks =
        _last?.fallback === true ? _last : _last?.fallback || null;
      return {
        count: item ? 1 : 0,
        countLabel: item?.datetime
          ? String(item.datetime).slice(0, 10)
          : undefined,
        lastUpdate: _lastUpdate,
        error: _lastError,
        source: 'Copernicus Sentinel · free, no key',
        coverage: item
          ? `${_kind === 's1' ? 'S1 SAR (radar, not photo)' : 'S2 10m'} · ${item.datetime || 'unknown date'} · cloud ${item.cloudCover ?? '?'}%`
          : fallbackLinks
            ? 'Catalogue unreachable — open Copernicus Browser link-out.'
            : 'No item yet — enable over land, or open Copernicus Browser link-out.',
        browserUrl: item?.browserUrl || fallbackLinks?.browserUrl || null,
      };
    },

    destroy() {
      _request?.abort();
      _request = null;
      _enabled = false;
      removeSentinelSection();
      overlayHost?.clearSource?.('sentinel-free');
      if (_viewer && _dataSource) {
        try {
          _viewer.dataSources.remove(_dataSource, true);
        } catch {
          /* best effort */
        }
      }
      _viewer = null;
      _dataSource = null;
    },
  };

  function _renderSnapshot(snapshot, center) {
    if (!_dataSource) return;
    _dataSource.entities.removeAll();
    const item = snapshot?.item;
    const halfDeg = 0.15;
    const rectangle = Cesium.Rectangle.fromDegrees(
      center.lon - halfDeg,
      center.lat - halfDeg,
      center.lon + halfDeg,
      center.lat + halfDeg,
    );
    const isSar = (_last?.kind || snapshot?.kind || _kind) === 's1';
    _dataSource.entities.add(
      new Cesium.Entity({
        id: `sentinel:${snapshot?.kind || _kind}:${center.lat.toFixed(2)},${center.lon.toFixed(2)}`,
        rectangle: {
          coordinates: rectangle,
          material: new Cesium.ColorMaterialProperty(
            isSar
              ? Cesium.Color.GRAY.withAlpha(0.25)
              : Cesium.Color.CYAN.withAlpha(0.15),
          ),
          outline: true,
          outlineColor: isSar ? Cesium.Color.LIGHTGRAY : Cesium.Color.CYAN,
          outlineWidth: 2,
          heightReference: Cesium.HeightReference.CLAMP_TO_GROUND,
        },
        properties: {
          datetime: item?.datetime || null,
          cloudCover: item?.cloudCover ?? null,
          browserUrl: item?.browserUrl || null,
          sar: isSar,
        },
      }),
    );
    overlayHost?.setVisible?.('sentinel-free', true);
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
