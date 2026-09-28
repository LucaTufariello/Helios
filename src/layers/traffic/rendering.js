import * as Cesium from 'cesium';
import { flowBucket } from '../../data/trafficFlowStyle.js';

// Bounded geometry, updated only when source data or the viewport changes.
const MAX_FLOW_ROADS = 1200;
const COLORS = {
  free: '#64b98b',
  slow: '#d7b461',
  jam: '#d97968',
  closed: '#b992d1',
};

export function createRendering({ state, parts }) {
  function visibleRoadsForAltitude(roads, altitude) {
    return altitude > 5000
      ? roads.filter((road) =>
          ['motorway', 'trunk', 'primary'].includes(road.type),
        )
      : roads;
  }

  function removeHeatLines() {
    state._flowLines?.removeAll();
    for (const primitive of state._flowGroundPrimitives || [])
      state._viewer?.scene?.groundPrimitives?.remove(primitive);
    state._flowGroundPrimitives = [];
    state._bucketCounts = { free: 0, slow: 0, jam: 0 };
    state._closedRoads = 0;
    state._truncated = false;
    state._viewer?.scene?.requestRender?.();
    state._heatLineCount = 0;
    state._count = 0;
  }

  function rebuildHeatLines(roads) {
    removeHeatLines();

    state._closedRoads = 0;
    state._truncated = false;
    if (!state._enabled || !state._liveMode || state._flowError) return;
    const batches = new Map();
    for (const road of roads) {
      // Unknown coverage never creates a marker or an invented vehicle.
      if (!road.flow || !Number.isFinite(road.flow.level)) continue;
      if (state._count >= MAX_FLOW_ROADS) {
        state._truncated = true;
        break;
      }
      const bucket = road.flow.closure ? 'closed' : flowBucket(road.flow.level);
      if (!bucket) continue;
      if (state._groundSupported) {
        if (!batches.has(bucket)) batches.set(bucket, []);
        batches.get(bucket).push(
          new Cesium.GeometryInstance({
            geometry: new Cesium.GroundPolylineGeometry({
              positions: road.waypoints,
              width: bucket === 'closed' ? 4 : 3,
            }),
          }),
        );
      } else
        state._flowLines?.add({
          positions: road.waypoints,
          width: bucket === 'closed' ? 4 : 3,
          material: Cesium.Material.fromType('Color', {
            color: Cesium.Color.fromCssColorString(COLORS[bucket]),
          }),
        });
      if (bucket === 'closed') state._closedRoads++;
      else state._bucketCounts[bucket]++;
      state._count++;
    }
    for (const [bucket, geometryInstances] of batches) {
      const primitive = new Cesium.GroundPolylinePrimitive({
        geometryInstances,
        appearance: new Cesium.PolylineMaterialAppearance({
          material: Cesium.Material.fromType('Color', {
            color: Cesium.Color.fromCssColorString(COLORS[bucket]),
          }),
        }),
        classificationType: Cesium.ClassificationType.BOTH,
      });
      state._viewer.scene.groundPrimitives.add(primitive);
      state._flowGroundPrimitives.push(primitive);
    }
    state._heatLineCount = state._count;
    state._viewer?.scene?.requestRender?.();
  }

  function renderRoadsForAltitude(roads, altitude, label, trace = null) {
    state._roads = roads;
    state._lastRenderAltitude = altitude;
    rebuildHeatLines(visibleRoadsForAltitude(roads, altitude));
    // Preserve optional timing diagnostics without any animation loop.
    if (trace) {
      const timingState = parts.timing.trafficTimingRenderState(trace, label);
      parts.timing.trafficTimingMark(timingState, 'road-flow-rendered', {
        count: state._count,
      });
    }
  }
  return {
    visibleRoadsForAltitude,
    removeHeatLines,
    rebuildHeatLines,
    renderRoadsForAltitude,
  };
}
