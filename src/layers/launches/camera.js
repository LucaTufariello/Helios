import * as Cesium from 'cesium';
import {
  MISSION_CLOSE_VIEW_RANGE_M,
  MISSION_GLOBE_VIEW_RANGE_M,
} from './policy.js';

export function createCamera({ state: layerState, services, parts, source }) {
  function cameraHeadingForPath(path, progress, fallback = Math.PI) {
    if (!path?.length) return fallback;
    const current = parts.paths.samplePath(path, progress);
    const currentCartographic =
      Cesium.Ellipsoid.WGS84.cartesianToCartographic(current);
    if (!currentCartographic) return fallback;
    for (const step of [0.01, 0.025, 0.05, 0.1, 0.2]) {
      const next = parts.paths.samplePath(path, Math.min(1, progress + step));
      const nextCartographic =
        Cesium.Ellipsoid.WGS84.cartesianToCartographic(next);
      if (!nextCartographic) continue;
      const geodesic = new Cesium.EllipsoidGeodesic(
        currentCartographic,
        nextCartographic,
      );
      if (
        geodesic.surfaceDistance > 10 &&
        Number.isFinite(geodesic.startHeading)
      ) {
        return Cesium.Math.zeroToTwoPi(geodesic.startHeading);
      }
    }
    return fallback;
  }

  function missionZoomPitch(rangeM) {
    const range = Math.max(0, Number(rangeM) || 0);
    const blend = Cesium.Math.clamp(
      (Math.log(Math.max(range, MISSION_CLOSE_VIEW_RANGE_M)) -
        Math.log(MISSION_CLOSE_VIEW_RANGE_M)) /
        (Math.log(MISSION_GLOBE_VIEW_RANGE_M) -
          Math.log(MISSION_CLOSE_VIEW_RANGE_M)),
      0,
      1,
    );
    return Cesium.Math.lerp(
      Cesium.Math.toRadians(-42),
      -Cesium.Math.PI_OVER_TWO,
      blend,
    );
  }

  return {
    cameraHeadingForPath,
    missionZoomPitch,
  };
}
