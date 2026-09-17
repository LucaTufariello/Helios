/**
 * Sentinel latest-clear source (portable — no Cesium, no browser globals).
 *
 * Fetches same-origin `/api/sentinel2-latest` / `/api/sentinel1-latest`
 * (never the OData catalogue directly). Keyless: without CDSE creds the proxy
 * still returns public catalogue metadata + browser link-outs, so the source
 * surfaces a graceful fallback instead of a hard error.
 */

function endpointFor(kind) {
  return kind === 's1' ? '/api/sentinel1-latest' : '/api/sentinel2-latest';
}

export function createSentinelSource({
  fetchImpl = (...args) => globalThis.fetch(...args),
} = {}) {
  return {
    async getSnapshot({ lat, lon, kind = 's2', signal } = {}) {
      signal?.throwIfAborted();
      if (!Number.isFinite(lat) || !Number.isFinite(lon)) {
        throw new Error('Sentinel snapshot requires finite lat/lon');
      }
      const endpoint = endpointFor(kind);
      const url = `${endpoint}?lat=${encodeURIComponent(lat)}&lon=${encodeURIComponent(lon)}`;
      const response = await fetchImpl(url, { signal, cache: 'no-store' });
      let payload;
      try {
        payload = await response.json();
      } catch {
        /* status below remains authoritative */
      }
      signal?.throwIfAborted();
      if (!response.ok) {
        // The proxy answers 502 (upstream down, no cache) with link-outs and
        // 400 (bad coords) the same way — both stay usable snapshots so the
        // layer remains ON with guidance instead of failing its enable.
        if (payload && (payload.fallback || payload.item)) {
          return {
            fallback: true,
            kind: payload.kind || kind,
            lat: Number.isFinite(payload.lat) ? payload.lat : lat,
            lon: Number.isFinite(payload.lon) ? payload.lon : lon,
            item: payload.item || null,
            browserUrl:
              payload.fallback?.browserUrl || payload.item?.browserUrl || null,
            eoBrowserUrl:
              payload.fallback?.eoBrowserUrl ||
              payload.item?.eoBrowserUrl ||
              null,
          };
        }
        throw new Error(`Sentinel HTTP ${response.status}`);
      }
      return payload;
    },
  };
}

export function sentinelBrowserUrls(lat, lon) {
  return {
    copernicus: `https://browser.dataspace.copernicus.eu/?zoom=12&lat=${lat}&lng=${lon}&themeId=DEFAULT-THEME`,
    eoBrowser: `https://apps.sentinel-hub.com/eo-browser/?lat=${lat}&lng=${lon}&zoom=12`,
  };
}
