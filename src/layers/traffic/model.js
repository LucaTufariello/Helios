import * as Cesium from 'cesium';
import { MAX_WAYPOINTS_PER_ROAD, DOT_HEIGHT_OFFSET } from './policy.js';

export function createModel({ state: layerState }) {
  function parseRoads(roadData) {
    if (!roadData || !roadData.roads) {
      return [];
    }

    const roads = [];
    for (const road of roadData.roads) {
      if (!road.coordinates || road.coordinates.length < 2) continue;

      const rawCoords = road.coordinates;

      // Sub-sample long polylines: keep every Nth vertex to stay within budget
      const simplifyStep =
        rawCoords.length > MAX_WAYPOINTS_PER_ROAD
          ? Math.ceil(rawCoords.length / MAX_WAYPOINTS_PER_ROAD)
          : 1;
      const coords = [];
      for (let i = 0; i < rawCoords.length; i += simplifyStep) {
        coords.push(rawCoords[i]);
      }

      // Ensure the original endpoint is always preserved
      const last = rawCoords[rawCoords.length - 1];
      const tail = coords[coords.length - 1];
      if (!tail || tail[0] !== last[0] || tail[1] !== last[1]) {
        coords.push(last);
      }

      if (coords.length < 2) continue;

      const type = road.type;
      const oneway = road.oneway;

      // Sample terrain height once at the road start to avoid per-vertex cost
      let baseHeight = 0;
      const firstCoord = coords[0];
      if (layerState._viewer?.scene?.sampleHeightSupported && firstCoord) {
        const carto = Cesium.Cartographic.fromDegrees(
          firstCoord[0],
          firstCoord[1],
        );
        const sampled = layerState._viewer.scene.sampleHeight(carto);
        if (Number.isFinite(sampled)) baseHeight = sampled;
      }

      // Pre-compute static road geometry at the local surface height.
      const waypoints = coords.map(([lng, lat]) => {
        const h = baseHeight + DOT_HEIGHT_OFFSET;
        return Cesium.Cartesian3.fromDegrees(lng, lat, h);
      });

      roads.push({ coords, type, oneway, waypoints });
    }

    return roads;
  }

  function trafficFeedPresentation({
    liveMode = false,
    fetching = false,
    flowError = null,
    coveragePct = 0,
    statusUnavailable = false,
    keyRequired = false,
  } = {}) {
    // `mode` is the CONFIGURED source (live key present vs keyless), not this
    // instant's health — health rides on `error`.
    const mode = liveMode ? 'live' : 'unavailable';
    if (keyRequired) {
      const label = 'KEY REQUIRED — add TomTom key for live';
      return { mode, error: 'KEY REQUIRED', loadingLabel: label };
    }
    if (liveMode && flowError) {
      // One string for both fields. The manager's meta line renders `error` and
      // drops `loadingLabel` in its error branch, so the copy has to BE the
      // error text.
      const degraded = `FLOW OFFLINE — ${flowError}`;
      return { mode, error: degraded, loadingLabel: degraded };
    }
    if (liveMode) {
      return {
        mode,
        error: null,
        loadingLabel: fetching
          ? 'Refreshing road flow'
          : `TomTom flow · ${coveragePct}% roads matched`,
      };
    }
    // The status probe itself failed: the layer loads nothing either way.
    return {
      mode,
      error: 'traffic service unreachable',
      loadingLabel: 'traffic service unreachable',
    };
  }

  return { parseRoads, trafficFeedPresentation };
}
