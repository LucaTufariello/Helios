/**
 * Solar terminator math (pure, no Cesium dependency).
 *
 * Computes the subsolar point and the day/night great-circle so the globe can
 * draw an honest daylight line. Used by the Sat Fresh layer (F5) to explain
 * why the baked basemap looks like daylight at night over places like Milan.
 */

const DEG = Math.PI / 180;
const RAD = 180 / Math.PI;

/** Subsolar point for `at` (UTC): { lat, lon } in degrees. */
export function subsolarPoint(at = new Date()) {
  const date = at instanceof Date ? at : new Date(at);
  const startOfYear = Date.UTC(date.getUTCFullYear(), 0, 0);
  const dayOfYear =
    Math.floor((date.getTime() - startOfYear) / 86_400_000) +
    date.getUTCHours() / 24 +
    date.getUTCMinutes() / 1440;
  // Solar declination (~23.44 deg amplitude).
  const declination = -23.44 * Math.cos((DEG * 360 * (dayOfYear + 10)) / 365);
  // Subsolar longitude from UTC time (solar noon ≈ 12:00 UTC at lon 0).
  const utcMinutes =
    date.getUTCHours() * 60 + date.getUTCMinutes() + date.getUTCSeconds() / 60;
  let lon = 180 - utcMinutes * 0.25;
  while (lon > 180) lon -= 360;
  while (lon < -180) lon += 360;
  return { lat: declination, lon };
}

/** Sun elevation in degrees for an observer (negative = below horizon). */
export function sunElevation(lat, lon, at = new Date()) {
  const sub = subsolarPoint(at);
  const latRad = lat * DEG;
  const decRad = sub.lat * DEG;
  let hourAngle = (lon - sub.lon) * DEG;
  const sinEl =
    Math.sin(latRad) * Math.sin(decRad) +
    Math.cos(latRad) * Math.cos(decRad) * Math.cos(hourAngle);
  return Math.asin(Math.max(-1, Math.min(1, sinEl))) * RAD;
}

/** True when the sun is below the horizon for the observer. */
export function isNight(lat, lon, at = new Date()) {
  return sunElevation(lat, lon, at) < 0;
}

/**
 * Day/night terminator polyline for `at`: [{ lat, lon }] around the globe.
 * For each longitude, latitude where the sun sits exactly on the horizon.
 */
export function terminatorPositions(at = new Date(), stepDeg = 2) {
  const sub = subsolarPoint(at);
  const decRad = sub.lat * DEG;
  const tanDec = Math.tan(decRad);
  const positions = [];
  for (let lon = -180; lon <= 180; lon += stepDeg) {
    const hourAngle = (lon - sub.lon) * DEG;
    const cosH = Math.cos(hourAngle);
    let lat;
    if (Math.abs(tanDec) < 1e-9) {
      lat = cosH > 0 ? 0 : 90 * Math.sign(sub.lat || 1);
    } else {
      lat = Math.atan(-cosH / tanDec) * RAD;
    }
    if (!Number.isFinite(lat)) continue;
    positions.push({ lat: Math.max(-90, Math.min(90, lat)), lon });
  }
  return positions;
}
