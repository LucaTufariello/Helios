import * as Cesium from 'cesium';

export const ENERGY_OVERLAY_SOURCE_ID = 'energy';

const KIND_COLORS = Object.freeze({
  plant: '#ffb347',
  refinery: '#ff6a3d',
  substation: '#7fa8c9',
});

const KIND_LABELS = Object.freeze({
  plant: 'Power plant',
  refinery: 'Refinery',
  substation: 'Substation',
});

/** Own one energy-site display and its refresh lifecycle. */
export function createEnergyLayer({ source, overlayHost } = {}) {
  if (typeof source?.getSites !== 'function')
    throw new TypeError('Energy layer requires a sites source');
  if (!overlayHost)
    throw new TypeError('Energy layer requires an overlay host');
  let _viewer = null;
  let _request = null;
  let _dataSource = null;
  let _count = 0;
  let _byKind = null;
  let _saturated = false;
  let _lastUpdate = null;
  let _lastError = null;
  let _enabled = false;

  const layer = {
    id: 'energy',
    name: 'Energy & Refining',
    icon: '⚡',
    source: 'OpenStreetMap · community mapped',
    showInTogglePanel: true,
    updateInterval: 300_000,

    init(viewer) {
      if (_viewer) throw new Error('Energy layer is already initialized');
      _viewer = viewer;
      _dataSource = new Cesium.CustomDataSource('energy');
      _dataSource.show = false;
      viewer.dataSources.add(_dataSource);
      overlayHost.setVisible(ENERGY_OVERLAY_SOURCE_ID, false);
    },

    enable() {
      _enabled = true;
      if (_dataSource) _dataSource.show = true;
      overlayHost.setVisible(ENERGY_OVERLAY_SOURCE_ID, true);
      void layer.refresh();
    },

    disable() {
      _request?.abort();
      _request = null;
      _enabled = false;
      if (_dataSource) {
        _dataSource.entities.removeAll();
        _dataSource.show = false;
      }
      overlayHost.clearSource(ENERGY_OVERLAY_SOURCE_ID);
      overlayHost.setVisible(ENERGY_OVERLAY_SOURCE_ID, false);
    },

    async refresh(viewport) {
      if (!_enabled || !_viewer) return true;
      const box =
        viewport && isBox(viewport) ? viewport : _viewportBox(_viewer);
      // Global/outing views require a zoom before a bounded request.
      if (!box) return true;
      _request?.abort();
      const request = new AbortController();
      _request = request;
      try {
        const { sites, saturated } = await source.getSites(box, {
          signal: request.signal,
        });
        if (request.signal.aborted || _request !== request || !_enabled)
          return false;
        _renderSites(sites);
        _count = sites.length;
        _byKind = countByKind(sites);
        _saturated = saturated === true;
        _lastUpdate = Date.now();
        _lastError = null;
        return true;
      } catch (error) {
        if (request.signal.aborted || _request !== request || !_enabled)
          return false;
        _lastError = error?.message || 'Energy source unavailable';
        return false;
      } finally {
        if (_request === request) _request = null;
      }
    },

    async update() {
      return layer.refresh(undefined);
    },

    getAnalystRecords(maxCount = 2000) {
      if (!_dataSource || !_dataSource.show) return [];
      const entities = _dataSource.entities.values;
      if (!entities.length) return [];
      const limit = Number.isFinite(maxCount)
        ? Math.max(1, Math.floor(maxCount))
        : 2000;
      const now = Cesium.JulianDate.now();
      const result = [];
      for (const entity of entities) {
        if (result.length >= limit) break;
        const cartesian = entity.position
          ? entity.position.getValue(now)
          : null;
        const carto = cartesian
          ? Cesium.Cartographic.fromCartesian(cartesian)
          : null;
        const p = entity.properties;
        result.push({
          name: p?.name?.getValue(now) ?? null,
          kind: p?.kind?.getValue(now) ?? null,
          lat: carto ? Cesium.Math.toDegrees(carto.latitude) : null,
          lon: carto ? Cesium.Math.toDegrees(carto.longitude) : null,
        });
      }
      return result;
    },

    getStats() {
      const parts = [];
      if (_byKind) {
        for (const kind of ['plant', 'refinery', 'substation']) {
          if (_byKind[kind]) parts.push(`${_byKind[kind]} ${kind}s`);
        }
      }
      return {
        count: _count,
        lastUpdate: _lastUpdate,
        error: _lastError,
        source: 'OpenStreetMap · community mapped, incomplete by nature',
        coverage:
          _count > 0
            ? `${parts.join(' · ')}${_saturated ? ' · viewport saturated, zoom in' : ''}`
            : 'No sites yet — zoom to a populated viewport.',
      };
    },

    destroy() {
      _request?.abort();
      _request = null;
      _viewer = null;
      _enabled = false;
      overlayHost.clearSource(ENERGY_OVERLAY_SOURCE_ID);
      overlayHost.setVisible(ENERGY_OVERLAY_SOURCE_ID, false);
      _count = 0;
      _byKind = null;
      _lastUpdate = null;
      _lastError = null;
    },
  };

  function _renderSites(sites) {
    if (!_dataSource) return;
    _dataSource.entities.removeAll();
    for (const site of sites) {
      const color = Cesium.Color.fromCssColorString(
        KIND_COLORS[site.kind] || '#ffffff',
      );
      const labeled = site.kind !== 'substation';
      _dataSource.entities.add(
        new Cesium.Entity({
          id: `energy:${site.id}`,
          position: Cesium.Cartesian3.fromDegrees(site.lon, site.lat),
          point: {
            pixelSize: site.kind === 'substation' ? 5 : 8,
            color,
            outlineColor: Cesium.Color.BLACK.withAlpha(0.7),
            outlineWidth: 1,
            heightReference: Cesium.HeightReference.CLAMP_TO_GROUND,
          },
          label: labeled
            ? {
                text: site.name || KIND_LABELS[site.kind] || site.kind,
                font: '10px JetBrains Mono, monospace',
                fillColor: Cesium.Color.WHITE,
                outlineColor: Cesium.Color.BLACK.withAlpha(0.8),
                outlineWidth: 2,
                style: Cesium.LabelStyle.FILL_AND_OUTLINE,
                pixelOffset: new Cesium.Cartesian2(0, -14),
                heightReference: Cesium.HeightReference.CLAMP_TO_GROUND,
              }
            : undefined,
          properties: {
            name: site.name,
            kind: site.kind,
          },
        }),
      );
    }
  }

  return layer;
}

function isBox(box) {
  const { south, west, north, east } = box || {};
  return [south, west, north, east].every(Number.isFinite);
}

function countByKind(sites) {
  const counts = {};
  for (const site of sites) {
    if (!site?.kind) continue;
    counts[site.kind] = (counts[site.kind] || 0) + 1;
  }
  return counts;
}

/** Viewport bbox clamped for dense energy data; null on global views. */
export function energyViewportBox(viewer, maxSpanDeg = 2) {
  try {
    const rectangle = viewer?.camera?.computeViewRectangle?.(
      viewer?.scene?.globe?.ellipsoid,
    );
    if (!rectangle) return null;
    const south = Cesium.Math.toDegrees(rectangle.south);
    const north = Cesium.Math.toDegrees(rectangle.north);
    const west = Cesium.Math.toDegrees(rectangle.west);
    const east = Cesium.Math.toDegrees(rectangle.east);
    if (
      ![south, north, west, east].every(Number.isFinite) ||
      east <= west ||
      north - south > maxSpanDeg ||
      east - west > maxSpanDeg
    )
      return null;
    return { south, west, north, east };
  } catch {
    return null;
  }
}

function _viewportBox(viewer) {
  return energyViewportBox(viewer);
}
