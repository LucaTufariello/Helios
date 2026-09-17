import path from 'node:path';
import { promises as fsp } from 'node:fs';

import { readResponseJsonCapped } from './common/http.js';
import { makeRateLimiter, clientKey } from './common/rate-limit.js';
import { requiredFiniteQueryNumber } from './common/query.js';

/**
 * Free Sentinel latest-clear proxy (no paid tasking).
 *
 * Upstream: Copernicus Dataspace OData catalogue (public search, keyless):
 *   GET https://catalogue.dataspace.copernicus.eu/odata/v1/Products
 *   (raw SENTINEL-1/2 collections no longer exist on the v1 STAC endpoint —
 *   only derived CCM/CLMS products — so search uses OData, which is keyless
 *   for discovery; auth is only needed for download)
 *
 * Routes:
 *   GET /api/sentinel2-latest?lat=45.46&lon=9.19 → latest SENTINEL-2 item
 *   GET /api/sentinel1-latest?lat=45.46&lon=9.19 → latest SENTINEL-1 item
 *   GET /api/sentinel2-latest/status, /api/sentinel1-latest/status
 *
 * Keyless by design for SEARCH (public OData). Optional free CDSE account
 * (`CDSE_USER`/`CDSE_PASS`, like the AISStream/FIRMS pattern) is reserved
 * for future authenticated download — search never requires it, and without
 * creds the proxy still returns item metadata + Copernicus Browser link-outs
 * instead of a hard error.
 *
 * Fixed upstream (no SSRF surface): client input is only lat/lon numbers.
 * Budgets: 60/min per-IP, 8MB response cap, identifying User-Agent,
 * 60min mem cache + disk under `.gev-cache/sentinel-*.json`.
 *
 * @returns {import('vite').Plugin}
 */
export function sentinelProxy() {
  const TTL_MS = 60 * 60_000;
  const ODATA_BASE_URL =
    'https://catalogue.dataspace.copernicus.eu/odata/v1/Products';
  const MAX_BODY_BYTES = 8 * 1024 * 1024;
  const CACHE_DIR = path.join(process.cwd(), '.gev-cache');
  const USER_AGENT =
    'gods-eye-view-sentinel-proxy/1.0 (+https://github.com/bilawalsidhu/gods-eye-view)';

  const limiter = makeRateLimiter({
    windowMs: 60_000,
    max: 60,
    globalMax: 200,
  });

  /** @type {Map<string, {at: number, payload: object}>} */
  const mem = new Map();
  /** @type {Map<string, Promise<object|null>}》} single-flight per cell */
  const inflight = new Map();
  let diskChecked = false;

  const hasCdseCreds = () =>
    Boolean(String(process.env.CDSE_USER || '').trim()) &&
    Boolean(String(process.env.CDSE_PASS || '').trim());

  function cellKey(kind, lat, lon) {
    return `${kind}:${Math.round(lat * 10) / 10}:${Math.round(lon * 10) / 10}`;
  }

  function diskPath(kind) {
    return path.join(CACHE_DIR, `sentinel-${kind}-latest.json`);
  }

  async function readDiskOnce() {
    if (diskChecked) return;
    diskChecked = true;
    for (const kind of ['s2', 's1']) {
      try {
        const parsed = JSON.parse(await fsp.readFile(diskPath(kind), 'utf8'));
        if (parsed && typeof parsed === 'object') {
          for (const [key, entry] of Object.entries(parsed)) {
            if (Number.isFinite(entry?.at) && entry?.payload)
              mem.set(key, entry);
          }
        }
      } catch {
        /* no disk cache yet */
      }
    }
  }

  async function writeDisk(kind) {
    try {
      const entries = Object.fromEntries(
        [...mem.entries()].filter(([key]) => key.startsWith(`${kind}:`)),
      );
      await fsp.mkdir(CACHE_DIR, { recursive: true });
      await fsp.writeFile(diskPath(kind), JSON.stringify(entries), 'utf8');
    } catch (err) {
      console.warn('[sentinel-proxy] cache write failed:', err?.message || err);
    }
  }

  function copernicusBrowserUrl(lat, lon, kind) {
    const base = 'https://browser.dataspace.copernicus.eu';
    return `${base}/?zoom=12&lat=${encodeURIComponent(lat)}&lng=${encodeURIComponent(lon)}&themeId=DEFAULT-THEME&datasetId=${kind === 's1' ? 'S1_AWS_IW_VVVH' : 'S2_L2A_CDAS'}`;
  }

  function eoBrowserUrl(lat, lon) {
    return `https://apps.sentinel-hub.com/eo-browser/?lat=${encodeURIComponent(lat)}&lng=${encodeURIComponent(lon)}&zoom=12`;
  }

  /** Cloud cover % from OData Attributes expansion (S2 optical only). */
  function cloudCoverOf(product) {
    const attributes = Array.isArray(product?.Attributes)
      ? product.Attributes
      : [];
    for (const attribute of attributes) {
      if (
        String(attribute?.Name || '').toLowerCase() === 'cloudcover' ||
        String(attribute?.Name || '').toLowerCase() === 'cloud_cover'
      ) {
        const value = Number(
          attribute?.Value ?? attribute?.doubleValue ?? attribute?.value,
        );
        if (Number.isFinite(value)) return value;
      }
    }
    return null;
  }

  /** Bounding box [west, south, east, north] from an OData GeoFootprint. */
  function bboxOf(product) {
    try {
      const coords = product?.GeoFootprint?.coordinates?.[0];
      if (!Array.isArray(coords) || !coords.length) return null;
      let west = Infinity;
      let south = Infinity;
      let east = -Infinity;
      let north = -Infinity;
      for (const point of coords) {
        const lon = Number(point?.[0]);
        const lat = Number(point?.[1]);
        if (!Number.isFinite(lon) || !Number.isFinite(lat)) continue;
        if (lon < west) west = lon;
        if (lat < south) south = lat;
        if (lon > east) east = lon;
        if (lat > north) north = lat;
      }
      if (!Number.isFinite(west)) return null;
      return [west, south, east, north];
    } catch {
      return null;
    }
  }

  function toItem(kind, lat, lon, product, cloudCover) {
    return {
      kind,
      datetime: product?.ContentDate?.Start || product?.OriginDate || null,
      cloudCover: Number.isFinite(cloudCover)
        ? Math.round(cloudCover * 10) / 10
        : null,
      bbox: bboxOf(product),
      collection: product?.Collection?.Name || null,
      id: product?.Id || null,
      name: product?.Name || null,
      thumbnail: null,
      browserUrl: copernicusBrowserUrl(lat, lon, kind),
      eoBrowserUrl: eoBrowserUrl(lat, lon),
    };
  }

  /**
   * Latest-clear product via the public OData catalogue (keyless search).
   * S2: 10 newest intersecting products, lowest cloud cover wins.
   * S1 (SAR, no cloud concept): most recent intersecting product wins.
   */
  async function queryOdata(collection, lat, lon, top, extraFilter = null) {
    const point = `POINT(${lon} ${lat})`;
    let filter =
      `Collection/Name eq '${collection}' and ` +
      `OData.CSC.Intersects(area=geography'SRID=4326;${point}')`;
    if (extraFilter) filter += ` and ${extraFilter}`;
    const params = new URLSearchParams({
      $filter: filter,
      $top: String(top),
      $orderby: 'ContentDate/Start desc',
      $expand: 'Attributes',
    });
    const res = await fetch(`${ODATA_BASE_URL}?${params.toString()}`, {
      headers: {
        'User-Agent': USER_AGENT,
        Accept: 'application/json',
      },
      signal: AbortSignal.timeout(20_000),
    });
    if (!res.ok) throw new Error(`OData HTTP ${res.status}`);
    const payload = await readResponseJsonCapped(res, MAX_BODY_BYTES);
    return Array.isArray(payload?.value) ? payload.value : [];
  }

  async function fetchLatest(kind, lat, lon) {
    // S2 prefers L2A (analysis-ready surface reflectance); L1C top-of-atmosphere
    // is the fallback when no L2A product intersects yet.
    let products;
    if (kind === 's1') {
      products = await queryOdata('SENTINEL-1', lat, lon, 5);
    } else {
      products = await queryOdata(
        'SENTINEL-2',
        lat,
        lon,
        10,
        "contains(Name,'MSIL2A')",
      );
      if (!products.length) {
        products = await queryOdata('SENTINEL-2', lat, lon, 10);
      }
    }
    if (!products.length) return null;
    if (kind === 's1') {
      return toItem(kind, lat, lon, products[0], null);
    }
    let best = null;
    let bestCloud = Infinity;
    for (const product of products) {
      const cloud = cloudCoverOf(product);
      const cloudValue = Number.isFinite(cloud) ? cloud : 100;
      if (cloudValue < bestCloud) {
        bestCloud = cloudValue;
        best = product;
      }
    }
    if (!best) return null;
    return toItem(
      kind,
      lat,
      lon,
      best,
      Number.isFinite(bestCloud) && bestCloud !== Infinity ? bestCloud : null,
    );
  }

  async function resolveLatest(kind, lat, lon) {
    const key = cellKey(kind, lat, lon);
    const cached = mem.get(key);
    if (cached && Date.now() - cached.at < TTL_MS) {
      return { payload: cached.payload, stale: false };
    }
    if (!inflight.has(key)) {
      inflight.set(
        key,
        (async () => {
          try {
            const item = await fetchLatest(kind, lat, lon);
            const payload = {
              fetchedAt: Date.now(),
              stale: false,
              ttlMs: TTL_MS,
              kind,
              lat,
              lon,
              item,
              hasCdseCreds: hasCdseCreds(),
              fallback: item
                ? null
                : {
                    note: 'No recent item in the OData catalogue — try the browser link-outs.',
                    browserUrl: copernicusBrowserUrl(lat, lon, kind),
                    eoBrowserUrl: eoBrowserUrl(lat, lon),
                  },
            };
            mem.set(key, { at: payload.fetchedAt, payload });
            await writeDisk(kind);
            return payload;
          } catch (err) {
            console.warn(
              `[sentinel-proxy] ${kind} fetch failed:`,
              err?.message || err,
            );
            return null;
          } finally {
            inflight.delete(key);
          }
        })(),
      );
    }
    const fresh = await inflight.get(key);
    if (fresh) return { payload: fresh, stale: false };
    if (cached)
      return { payload: { ...cached.payload, stale: true }, stale: true };
    return { payload: null, stale: false };
  }

  function makeHandler(kind) {
    return async (req, res) => {
      const sendJson = (status, obj) => {
        if (res.headersSent) return;
        res.writeHead(status, {
          'Content-Type': 'application/json',
          'Cache-Control': 'no-store',
        });
        res.end(JSON.stringify(obj));
      };
      try {
        if (!limiter(clientKey(req))) {
          sendJson(429, { error: 'rate_limited' });
          return;
        }
        const url = new URL(req.url || '/', 'http://local');
        const subPath = url.pathname || '/';
        await readDiskOnce();

        if (subPath === '/status') {
          sendJson(200, {
            hasKey: hasCdseCreds(),
            keyless: true,
            ttlMs: TTL_MS,
            cells: [...mem.keys()].filter((key) => key.startsWith(`${kind}:`))
              .length,
          });
          return;
        }

        const lat = requiredFiniteQueryNumber(url.searchParams, 'lat');
        const lon = requiredFiniteQueryNumber(url.searchParams, 'lon');
        if (
          lat === null ||
          lon === null ||
          !Number.isFinite(lat) ||
          !Number.isFinite(lon) ||
          lat < -90 ||
          lat > 90 ||
          lon < -180 ||
          lon > 180
        ) {
          sendJson(400, {
            error: 'bad_request',
            fallback: {
              browserUrl: 'https://browser.dataspace.copernicus.eu',
              eoBrowserUrl: 'https://apps.sentinel-hub.com/eo-browser/',
            },
          });
          return;
        }

        const { payload, stale } = await resolveLatest(kind, lat, lon);
        if (payload) {
          sendJson(200, { ...payload, stale });
          return;
        }
        sendJson(502, {
          error: 'sentinel fetch failed and no cache available',
          kind,
          lat,
          lon,
          fallback: {
            browserUrl: copernicusBrowserUrl(lat, lon, kind),
            eoBrowserUrl: eoBrowserUrl(lat, lon),
          },
        });
      } catch (err) {
        console.warn('[sentinel-proxy] error:', err?.message || err);
        if (!res.headersSent) {
          res.writeHead(500, {
            'Content-Type': 'application/json',
            'Cache-Control': 'no-store',
          });
          res.end(JSON.stringify({ error: 'sentinel proxy error' }));
        }
      }
    };
  }

  const installMiddleware = (server) => {
    server.middlewares.use('/api/sentinel2-latest', makeHandler('s2'));
    server.middlewares.use('/api/sentinel1-latest', makeHandler('s1'));
  };

  return {
    name: 'sentinel-proxy',
    configureServer: installMiddleware,
    configurePreviewServer: installMiddleware,
  };
}
