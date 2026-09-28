import * as Cesium from 'cesium';

export const GDACS_OVERLAY_SOURCE_ID = 'gdacs';

const SEVERITY_COLORS = Object.freeze({
  Red: '#f87171',
  Orange: '#fb923c',
  Green: '#4ade80',
});

const TYPE_LABELS = Object.freeze({
  FL: 'Flood',
  EQ: 'Earthquake',
  TC: 'Cyclone',
  VO: 'Volcano',
  DR: 'Drought',
  WF: 'Wildfire',
});

/** Own one disaster-alert display and its refresh lifecycle. */
export function createGdacsLayer({ source, overlayHost } = {}) {
  if (typeof source?.getSnapshot !== 'function')
    throw new TypeError('Disaster-alerts layer requires a snapshot source');
  if (!overlayHost)
    throw new TypeError('Disaster-alerts layer requires an overlay host');
  let _viewer = null;
  let _request = null;
  let _dataSource = null;
  let _count = 0;
  let _bySeverity = null;
  let _stale = false;
  let _lastUpdate = null;
  let _lastError = null;
  let _enabled = false;

  const layer = {
    id: 'gdacs',
    name: 'Disaster Alerts',
    icon: '🌊',
    source: 'GDACS · public domain',
    showInTogglePanel: true,
    updateInterval: 1_800_000,

    init(viewer) {
      if (_viewer)
        throw new Error('Disaster-alerts layer is already initialized');
      _viewer = viewer;
      _dataSource = new Cesium.CustomDataSource('gdacs');
      _dataSource.show = false;
      viewer.dataSources.add(_dataSource);
      overlayHost.setVisible(GDACS_OVERLAY_SOURCE_ID, false);
    },

    enable() {
      _enabled = true;
      if (_dataSource) _dataSource.show = true;
      overlayHost.setVisible(GDACS_OVERLAY_SOURCE_ID, true);
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
      overlayHost.clearSource(GDACS_OVERLAY_SOURCE_ID);
      overlayHost.setVisible(GDACS_OVERLAY_SOURCE_ID, false);
    },

    async refresh() {
      if (!_enabled || !_viewer) return true;
      _request?.abort();
      const request = new AbortController();
      _request = request;
      try {
        const snapshot = await source.getSnapshot({ signal: request.signal });
        if (request.signal.aborted || _request !== request || !_enabled)
          return false;
        const alerts = Array.isArray(snapshot?.alerts) ? snapshot.alerts : [];
        _renderAlerts(alerts);
        _count = alerts.length;
        _bySeverity = countBySeverity(alerts);
        _stale = snapshot?.stale === true;
        _lastUpdate = Date.now();
        _lastError = null;
        return true;
      } catch (error) {
        if (request.signal.aborted || _request !== request || !_enabled)
          return false;
        _lastError = error?.message || 'Disaster-alerts source unavailable';
        return false;
      } finally {
        if (_request === request) _request = null;
      }
    },

    async update() {
      return layer.refresh();
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
          title: p?.title?.getValue(now) ?? null,
          severity: p?.severity?.getValue(now) ?? null,
          type: p?.type?.getValue(now) ?? null,
          lat: carto ? Cesium.Math.toDegrees(carto.latitude) : null,
          lon: carto ? Cesium.Math.toDegrees(carto.longitude) : null,
        });
      }
      return result;
    },

    getStats() {
      const parts = [];
      if (_bySeverity) {
        for (const level of ['Red', 'Orange', 'Green']) {
          if (_bySeverity[level]) parts.push(`${_bySeverity[level]} ${level}`);
        }
      }
      return {
        count: _count,
        lastUpdate: _lastUpdate,
        error: _lastError,
        source: 'GDACS · public domain (UN/EU JRC)',
        coverage:
          _count > 0
            ? `${parts.join(' · ') || `${_count} alerts`}${_stale ? ' · cached' : ''}`
            : 'No alerts yet — enable to fetch the current feed.',
      };
    },

    destroy() {
      _request?.abort();
      _request = null;
      _viewer = null;
      _enabled = false;
      overlayHost.clearSource(GDACS_OVERLAY_SOURCE_ID);
      overlayHost.setVisible(GDACS_OVERLAY_SOURCE_ID, false);
      _count = 0;
      _bySeverity = null;
      _lastUpdate = null;
      _lastError = null;
    },
  };

  function _renderAlerts(alerts) {
    if (!_dataSource) return;
    _dataSource.entities.removeAll();
    for (const alert of alerts) {
      if (!Number.isFinite(alert?.lat) || !Number.isFinite(alert?.lon))
        continue;
      const color = Cesium.Color.fromCssColorString(
        SEVERITY_COLORS[alert.severity] || '#9aa4b2',
      );
      const major = alert.severity === 'Red' || alert.severity === 'Orange';
      _dataSource.entities.add(
        new Cesium.Entity({
          id: `gdacs:${alert.id}`,
          position: Cesium.Cartesian3.fromDegrees(alert.lon, alert.lat),
          point: {
            pixelSize: major ? 9 : 6,
            color,
            outlineColor: Cesium.Color.BLACK.withAlpha(0.7),
            outlineWidth: 1,
            heightReference: Cesium.HeightReference.CLAMP_TO_GROUND,
          },
          label: major
            ? {
                text: alert.title || TYPE_LABELS[alert.type] || 'Alert',
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
            title: alert.title,
            severity: alert.severity,
            type: alert.type,
            link: alert.link,
          },
        }),
      );
    }
  }

  return layer;
}

function countBySeverity(alerts) {
  const counts = {};
  for (const alert of alerts) {
    if (!alert?.severity) continue;
    counts[alert.severity] = (counts[alert.severity] || 0) + 1;
  }
  return counts;
}
