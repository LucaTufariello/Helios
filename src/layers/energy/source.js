/**
 * Energy-sites source (portable — no Cesium, no browser globals).
 *
 * Queries community-mapped power plants, substations and refineries through
 * the app's generic `/api/overpass` proxy (never an Overpass mirror
 * directly). Every selector is viewport-bounded, satisfying the proxy's
 * sanitizer; ways resolve via `out center` because plants and substations
 * are overwhelmingly mapped as areas.
 */

export const ENERGY_ELEMENT_CAP = 400;
export const ENERGY_MAX_SPAN_DEG = 2;

const KIND_BY_TAG = Object.freeze({
  'power=plant': 'plant',
  'power=substation': 'substation',
  'industrial=refinery': 'refinery',
});

function buildQuery(south, west, north, east) {
  const bbox = `${south},${west},${north},${east}`;
  const selectors = [
    `node["power"="plant"](${bbox})`,
    `way["power"="plant"](${bbox})`,
    `node["power"="substation"](${bbox})`,
    `way["power"="substation"](${bbox})`,
    `node["industrial"="refinery"](${bbox})`,
    `way["industrial"="refinery"](${bbox})`,
  ];
  return `[out:json][timeout:25];(${selectors.join(';')};);out center tags;`;
}

function kindOf(tags) {
  if (!tags || typeof tags !== 'object') return null;
  for (const [tag, kind] of Object.entries(KIND_BY_TAG)) {
    const [key, value] = tag.split('=');
    if (tags[key] === value) return kind;
  }
  return null;
}

/** Normalize one Overpass element to a site record, or null. */
export function normalizeEnergyElement(element) {
  const kind = kindOf(element?.tags);
  if (!kind) return null;
  let lat = Number(element?.lat);
  let lon = Number(element?.lon);
  if (!Number.isFinite(lat) || !Number.isFinite(lon)) {
    lat = Number(element?.center?.lat);
    lon = Number(element?.center?.lon);
  }
  if (!Number.isFinite(lat) || !Number.isFinite(lon)) return null;
  if (Math.abs(lat) > 90 || Math.abs(lon) > 180) return null;
  return {
    id: `${element?.type || 'node'}/${element?.id}`,
    kind,
    name:
      typeof element?.tags?.name === 'string' && element.tags.name.trim()
        ? element.tags.name.trim().slice(0, 120)
        : null,
    lat,
    lon,
  };
}

export function createEnergySource({
  fetchImpl = (...args) => globalThis.fetch(...args),
} = {}) {
  return {
    async getSites(box, { signal } = {}) {
      const { south, west, north, east } = box || {};
      if (
        ![south, west, north, east].every(Number.isFinite) ||
        south < -90 ||
        north > 90 ||
        west < -180 ||
        east > 180 ||
        north <= south ||
        east <= west ||
        north - south > ENERGY_MAX_SPAN_DEG ||
        east - west > ENERGY_MAX_SPAN_DEG
      )
        throw new TypeError('A small bounded energy viewport is required');
      signal?.throwIfAborted();
      const response = await fetchImpl('/api/overpass', {
        method: 'POST',
        headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
        body: `data=${encodeURIComponent(buildQuery(south, west, north, east))}`,
        signal,
      });
      let payload;
      try {
        payload = await response.json();
      } catch {
        throw new Error('Malformed energy snapshot');
      }
      signal?.throwIfAborted();
      if (!response.ok)
        throw new Error(
          payload?.error || `Energy feed HTTP ${response.status}`,
        );
      if (!Array.isArray(payload?.elements))
        throw new Error('Malformed energy snapshot');
      const sites = [];
      for (const element of payload.elements) {
        const site = normalizeEnergyElement(element);
        if (site) sites.push(site);
        if (sites.length >= ENERGY_ELEMENT_CAP) break;
      }
      return {
        sites,
        saturated: payload.elements.length >= ENERGY_ELEMENT_CAP,
      };
    },
  };
}
