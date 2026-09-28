/**
 * Atmosphere source (portable — no Cesium, no browser globals).
 *
 * Fetches same-origin `/api/atmosphere` (never Open-Meteo directly).
 * Keyless: the proxy serves cached snapshots when upstream is down, so the
 * source surfaces stale data with its age instead of a hard error.
 */

export function createAtmosphereSource({
  fetchImpl = (...args) => globalThis.fetch(...args),
} = {}) {
  return {
    async getSnapshot({ lat, lon, signal } = {}) {
      signal?.throwIfAborted();
      if (!Number.isFinite(lat) || !Number.isFinite(lon)) {
        throw new Error('Atmosphere snapshot requires finite lat/lon');
      }
      const url = `/api/atmosphere?lat=${encodeURIComponent(lat)}&lon=${encodeURIComponent(lon)}`;
      const response = await fetchImpl(url, { signal, cache: 'no-store' });
      let payload;
      try {
        payload = await response.json();
      } catch {
        /* status below remains authoritative */
      }
      signal?.throwIfAborted();
      if (!response.ok || !payload?.item) {
        // Serve-stale: a cached payload without a fresh item is still a
        // usable snapshot, marked stale by the proxy.
        if (payload?.item || payload?.stale) return payload;
        throw new Error(`Atmosphere HTTP ${response.status}`);
      }
      return payload;
    },
  };
}
