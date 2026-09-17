import * as Cesium from 'cesium';

export function createPaths({ state: layerState, services, parts, source }) {
  function orbitInsertionOffsetSeconds(launch) {
    const events = (launch.timeline || []).filter(
      (event) =>
        Number.isFinite(event.offsetSeconds) && event.offsetSeconds >= 0,
    );
    if (!events.length) return null;
    const deployment = events.filter((event) =>
      /deploy|payload separation|spacecraft separation|orbit insertion|injection/i.test(
        event.name,
      ),
    );
    if (deployment.length)
      return Math.max(...deployment.map((event) => event.offsetSeconds));
    const engineCutoff = events.filter((event) =>
      /seco|second engine cutoff/i.test(event.name),
    );
    if (engineCutoff.length)
      return Math.max(...engineCutoff.map((event) => event.offsetSeconds));
    return Math.max(...events.map((event) => event.offsetSeconds));
  }

  function estimatedOrbitPeriodSeconds(orbitPath) {
    if (!orbitPath?.length) return 5400;
    const meanRadius =
      orbitPath.reduce(
        (total, point) => total + Cesium.Cartesian3.magnitude(point),
        0,
      ) / orbitPath.length;
    return 2 * Math.PI * Math.sqrt(meanRadius ** 3 / 3.986004418e14);
  }

  function approximateOrbitPath(launch) {
    if (
      !launch.orbit?.name ||
      !Number.isFinite(launch.lat) ||
      !Number.isFinite(launch.lon)
    )
      return null;
    const orbitName = launch.orbit.name.toLowerCase();
    const altitude =
      orbitName.includes('geostationary') || orbitName.includes('transfer')
        ? 35786000
        : orbitName.includes('medium')
          ? 20200000
          : 550000;
    const radius = Cesium.Ellipsoid.WGS84.maximumRadius + altitude;
    const longitude = Cesium.Math.toRadians(launch.lon);
    const latitude = Cesium.Math.toRadians(launch.lat);
    const up = new Cesium.Cartesian3(
      Math.cos(latitude) * Math.cos(longitude),
      Math.cos(latitude) * Math.sin(longitude),
      Math.sin(latitude),
    );
    const east = new Cesium.Cartesian3(
      -Math.sin(longitude),
      Math.cos(longitude),
      0,
    );
    const north = new Cesium.Cartesian3(
      -Math.sin(latitude) * Math.cos(longitude),
      -Math.sin(latitude) * Math.sin(longitude),
      Math.cos(latitude),
    );
    const isPolar = orbitName.includes('polar') || orbitName.includes('sun');
    const isWesternNorthAmerica =
      launch.lat > 20 &&
      launch.lat < 60 &&
      launch.lon > -140 &&
      launch.lon < -105;
    const launchAzimuthDeg = isPolar
      ? launch.lat >= 0
        ? 180
        : 0
      : isWesternNorthAmerica
        ? 190
        : 90;
    const launchAzimuth = Cesium.Math.toRadians(launchAzimuthDeg);
    const forward = Cesium.Cartesian3.normalize(
      Cesium.Cartesian3.add(
        Cesium.Cartesian3.multiplyByScalar(
          north,
          Math.cos(launchAzimuth),
          new Cesium.Cartesian3(),
        ),
        Cesium.Cartesian3.multiplyByScalar(
          east,
          Math.sin(launchAzimuth),
          new Cesium.Cartesian3(),
        ),
        new Cesium.Cartesian3(),
      ),
      new Cesium.Cartesian3(),
    );
    // A reconstructed orbit is an estimate, not a claim that the vehicle was
    // inserted directly above the pad. Offset the orbital plane downrange by a
    // small launch-to-insertion arc so a top-down view does not draw the entire
    // orbit on top of the launch site. The ascent still joins this ring at its
    // propagated insertion reference.
    const insertionArc = Cesium.Math.toRadians(isPolar ? 8 : 12);
    const orbitAnchor = Cesium.Cartesian3.normalize(
      Cesium.Cartesian3.add(
        Cesium.Cartesian3.multiplyByScalar(
          up,
          Math.cos(insertionArc),
          new Cesium.Cartesian3(),
        ),
        Cesium.Cartesian3.multiplyByScalar(
          forward,
          Math.sin(insertionArc),
          new Cesium.Cartesian3(),
        ),
        new Cesium.Cartesian3(),
      ),
      new Cesium.Cartesian3(),
    );
    const planeNormal = Cesium.Cartesian3.normalize(
      Cesium.Cartesian3.cross(orbitAnchor, forward, new Cesium.Cartesian3()),
      new Cesium.Cartesian3(),
    );
    const crossTrack = Cesium.Cartesian3.normalize(
      Cesium.Cartesian3.cross(
        planeNormal,
        orbitAnchor,
        new Cesium.Cartesian3(),
      ),
      new Cesium.Cartesian3(),
    );
    return Array.from({ length: 97 }, (_, index) => {
      const angle = (index / 96) * Math.PI * 2;
      return new Cesium.Cartesian3(
        radius *
          (Math.cos(angle) * orbitAnchor.x + Math.sin(angle) * crossTrack.x),
        radius *
          (Math.cos(angle) * orbitAnchor.y + Math.sin(angle) * crossTrack.y),
        radius *
          (Math.cos(angle) * orbitAnchor.z + Math.sin(angle) * crossTrack.z),
      );
    });
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

  function nearestOrbitIndex(orbitPath, referencePosition) {
    if (!orbitPath?.length || !referencePosition) return 0;
    const referenceDirection = Cesium.Cartesian3.normalize(
      referencePosition,
      new Cesium.Cartesian3(),
    );
    let bestIndex = 0;
    let bestDot = -Number.MAX_VALUE;
    orbitPath.forEach((candidate, index) => {
      const direction = Cesium.Cartesian3.normalize(
        candidate,
        new Cesium.Cartesian3(),
      );
      const dot = Cesium.Cartesian3.dot(referenceDirection, direction);
      if (dot > bestDot) {
        bestDot = dot;
        bestIndex = index;
      }
    });
    return bestIndex;
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

  function landingEndpoint(stage, launch, insertionPosition) {
    if (Number.isFinite(stage.lat) && Number.isFinite(stage.lon)) {
      return { lat: stage.lat, lon: stage.lon, accuracy: 'CONFIRMED' };
    }
    const recoveryIdentity =
      `${stage.recoveryType || ''} ${stage.destination || ''}`.toLowerCase();
    if (
      /return to launch site|rtls|launch site|landing zone/.test(
        recoveryIdentity,
      )
    ) {
      return { lat: launch.lat, lon: launch.lon, accuracy: 'PAD / RTLS' };
    }
    if (!(stage.downrangeKm > 0) || !insertionPosition) return null;
    const ellipsoid = Cesium.Ellipsoid.WGS84;
    const start = Cesium.Cartographic.fromDegrees(launch.lon, launch.lat);
    const insertion = ellipsoid.cartesianToCartographic(insertionPosition);
    if (!insertion) return null;
    const geodesic = new Cesium.EllipsoidGeodesic(start, insertion, ellipsoid);
    const bearing = geodesic.startHeading;
    const angularDistance =
      (stage.downrangeKm * 1000) / ellipsoid.maximumRadius;
    const lat1 = start.latitude;
    const lon1 = start.longitude;
    const lat = Math.asin(
      Math.sin(lat1) * Math.cos(angularDistance) +
        Math.cos(lat1) * Math.sin(angularDistance) * Math.cos(bearing),
    );
    const lon =
      lon1 +
      Math.atan2(
        Math.sin(bearing) * Math.sin(angularDistance) * Math.cos(lat1),
        Math.cos(angularDistance) - Math.sin(lat1) * Math.sin(lat),
      );
    return {
      lat: Cesium.Math.toDegrees(lat),
      lon: Cesium.Math.toDegrees(Cesium.Math.negativePiToPi(lon)),
      accuracy: 'EST. DOWNRANGE',
    };
  }

  return {
    orbitInsertionOffsetSeconds,
    estimatedOrbitPeriodSeconds,
    approximateOrbitPath,
    samplePath,
    orbitProgressAtTime,
    nearestOrbitIndex,
    surfaceSafeSegment,
    surfaceSafePath,
    landingEndpoint,
  };
}
