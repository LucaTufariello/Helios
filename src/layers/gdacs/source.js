/**
 * Disaster-alerts source (portable — no Cesium, no browser globals).
 *
 * Fetches same-origin `/api/gdacs` (never gdacs.org directly). The feed is
 * global, so no viewport is needed: one snapshot carries every current
 * alert and the layer filters nothing.
 */

export function createGdacsSource({
  fetchImpl = (...args) => globalThis.fetch(...args),
} = {}) {
  return {
    async getSnapshot({ signal } = {}) {
      signal?.throwIfAborted();
      const response = await fetchImpl('/api/gdacs', {
        signal,
        cache: 'no-store',
      });
      let payload;
      try {
        payload = await response.json();
      } catch {
        /* status below remains authoritative */
      }
      signal?.throwIfAborted();
      if (!response.ok || !Array.isArray(payload?.alerts)) {
        if (Array.isArray(payload?.alerts)) return payload;
        throw new Error(`Disaster-alerts HTTP ${response.status}`);
      }
      return payload;
    },
  };
}
