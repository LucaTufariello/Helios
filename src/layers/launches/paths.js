import * as Cesium from 'cesium';

export function createPaths({ state: layerState, services, parts, source }) {
  /** Legacy API retained as unavailable: launch metadata cannot establish an orbit. */
  function approximateOrbitPath() {
    return null;
  }

  function samplePath(path, progress, result) {
    if (!path?.length) return undefined;
    // Degenerate returns clone into `result` when provided — handing back a
    // path vertex would let an in-place caller mutate the path geometry.
    if (path.length === 1)
      return result ? Cesium.Cartesian3.clone(path[0], result) : path[0];
    let distances = layerState._pathDistanceCache.get(path);
    if (!distances) {
      distances = new Float64Array(path.length);
      for (let index = 1; index < path.length; index++) {
        distances[index] =
          distances[index - 1] +
          Cesium.Cartesian3.distance(path[index - 1], path[index]);
      }
      layerState._pathDistanceCache.set(path, distances);
    }
    const totalDistance = distances.at(-1);
    if (!(totalDistance > 0))
      return result ? Cesium.Cartesian3.clone(path[0], result) : path[0];
    const targetDistance = Cesium.Math.clamp(progress, 0, 1) * totalDistance;
    let low = 1;
    let high = distances.length - 1;
    while (low < high) {
      const middle = Math.floor((low + high) / 2);
      if (distances[middle] < targetDistance) low = middle + 1;
      else high = middle;
    }
    const index = Math.max(0, low - 1);
    const segmentDistance = distances[index + 1] - distances[index];
    const segmentProgress =
      segmentDistance > 0
        ? (targetDistance - distances[index]) / segmentDistance
        : 0;
    return Cesium.Cartesian3.lerp(
      path[index],
      path[index + 1],
      segmentProgress,
      result || new Cesium.Cartesian3(),
    );
  }

  /**
   * Resolve a continuously advancing orbit fraction from a wall-clock epoch.
   * Fractional seconds prevent the selected satellite marker from jumping at
   * each second boundary and forcing a one-frame photoreal globe LOD pulse.
   * @param {number} nowMs Wall-clock epoch in milliseconds.
   * @param {number} periodSec Orbital period in seconds.
   * @returns {number} Normalized progress in the range [0, 1).
   */

  function orbitProgressAtTime(nowMs, periodSec) {
    const period = Math.max(1, Number(periodSec) || 1);
    return Cesium.Math.mod((Number(nowMs) || 0) / 1000, period) / period;
  }

  function surfaceSafeSegment(startPosition, endPosition) {
    const ellipsoid = Cesium.Ellipsoid.WGS84;
    const start = ellipsoid.cartesianToCartographic(startPosition);
    const end = ellipsoid.cartesianToCartographic(endPosition);
    if (!start || !end) return [startPosition, endPosition];
    const geodesic = new Cesium.EllipsoidGeodesic(
      new Cesium.Cartographic(start.longitude, start.latitude),
      new Cesium.Cartographic(end.longitude, end.latitude),
      ellipsoid,
    );
    // Keep the geometry surface-safe with enough samples that long
    // segments do not visibly corner at segment boundaries.
    const steps = Cesium.Math.clamp(
      Math.ceil(geodesic.surfaceDistance / 75000),
      2,
      256,
    );
    const positions = [];
    for (let index = 0; index <= steps; index++) {
      const fraction = index / steps;
      const eased = fraction * fraction * (3 - 2 * fraction);
      const cartographic = geodesic.interpolateUsingFraction(
        fraction,
        new Cesium.Cartographic(),
      );
      cartographic.height = Cesium.Math.lerp(start.height, end.height, eased);
      positions.push(ellipsoid.cartographicToCartesian(cartographic));
    }
    positions[0] = startPosition;
    positions[positions.length - 1] = endPosition;
    return positions;
  }

  function surfaceSafePath(controlPositions) {
    if (!controlPositions?.length) return [];
    if (controlPositions.length === 1) return controlPositions.slice();
    const path = [];
    for (let index = 0; index < controlPositions.length - 1; index++) {
      const segment = surfaceSafeSegment(
        controlPositions[index],
        controlPositions[index + 1],
      );
      path.push(...(index === 0 ? segment : segment.slice(1)));
    }
    return path;
  }

  function landingEndpoint(stage) {
    if (Number.isFinite(stage.lat) && Number.isFinite(stage.lon)) {
      return { lat: stage.lat, lon: stage.lon, accuracy: 'SOURCE REPORTED' };
    }
    return null;
  }

  return {
    approximateOrbitPath,
    samplePath,
    orbitProgressAtTime,
    surfaceSafeSegment,
    surfaceSafePath,
    landingEndpoint,
  };
}
