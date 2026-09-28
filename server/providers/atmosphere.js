import path from 'node:path';
import { promises as fsp } from 'node:fs';

import { readResponseJsonCapped } from './common/http.js';
import { makeRateLimiter, clientKey } from './common/rate-limit.js';
import { requiredFiniteQueryNumber } from './common/query.js';

/**
 * Free atmosphere proxy (no key, non-commercial use).
 *
 * Upstream: Open-Meteo (https://open-meteo.com, CC BY 4.0, keyless,
 * CORS-enabled). The free tier is for non-commercial use up to 10,000
 * calls/day — the proxy's per-IP throttle plus 15-minute cell cache keeps a
 * local deployment far under that. Fixed upstream hosts (no SSRF surface):
 * client input is only lat/lon numbers.
 *
 * Routes:
 *   GET /api/atmosphere?lat=45.46&lon=9.19 → current + today/tomorrow + EAQI
 *   GET /api/atmosphere/status
 *
 * Budgets: 60/min per-IP, 2MB response cap, identifying User-Agent,
 * 15min mem cache + disk under `.gev-cache/atmosphere.json`.
 *
 * @returns {import('vite').Plugin}
 */
export function atmosphereProxy() {
  const TTL_MS = 15 * 60_000;
  const MAX_BODY_BYTES = 2 * 1024 * 1024;
  const CACHE_DIR = path.join(process.cwd(), '.gev-cache');
  const USER_AGENT = 'gods-eye-view-atmosphere-proxy/1.0';

  const limiter = makeRateLimiter({
    windowMs: 60_000,
    max: 60,
    globalMax: 200,
  });

  /** @type {Map<string, {at: number, payload: object}>} */
  const mem = new Map();
  /** @type {Map<string, Promise<object|null>>} single-flight per cell */
  const inflight = new Map();
  let diskChecked = false;

  function cellKey(lat, lon) {
    return `${Math.round(lat * 10) / 10}:${Math.round(lon * 10) / 10}`;
  }

  function diskPath() {
    return path.join(CACHE_DIR, 'atmosphere.json');
  }

  async function readDiskOnce() {
    if (diskChecked) return;
    diskChecked = true;
    try {
      const parsed = JSON.parse(await fsp.readFile(diskPath(), 'utf8'));
      if (parsed && typeof parsed === 'object') {
        for (const [key, entry] of Object.entries(parsed)) {
          if (Number.isFinite(entry?.at) && entry?.payload) mem.set(key, entry);
        }
      }
    } catch {
      /* no disk cache yet */
    }
  }

  async function writeDisk() {
    try {
      await fsp.mkdir(CACHE_DIR, { recursive: true });
      await fsp.writeFile(
        diskPath(),
        JSON.stringify(Object.fromEntries(mem)),
        'utf8',
      );
    } catch (err) {
      console.warn(
        '[atmosphere-proxy] cache write failed:',
        err?.message || err,
      );
    }
  }

  /** WMO weather interpretation code → short honest label. */
  function weatherLabel(code) {
    const table = {
      0: 'Clear sky',
      1: 'Mainly clear',
      2: 'Partly cloudy',
      3: 'Overcast',
      45: 'Fog',
      48: 'Depositing rime fog',
      51: 'Light drizzle',
      53: 'Drizzle',
      55: 'Dense drizzle',
      56: 'Light freezing drizzle',
      57: 'Dense freezing drizzle',
      61: 'Slight rain',
      63: 'Rain',
      65: 'Heavy rain',
      66: 'Light freezing rain',
      67: 'Heavy freezing rain',
      71: 'Slight snow',
      73: 'Snow',
      75: 'Heavy snow',
      77: 'Snow grains',
      80: 'Slight showers',
      81: 'Showers',
      82: 'Violent showers',
      85: 'Slight snow showers',
      86: 'Heavy snow showers',
      95: 'Thunderstorm',
      96: 'Thunderstorm, slight hail',
      99: 'Thunderstorm, heavy hail',
    };
    return Object.hasOwn(table, code) ? table[code] : null;
  }

  function finite(value) {
    const number = Number(value);
    return Number.isFinite(number) ? number : null;
  }

  async function fetchJson(url) {
    const res = await fetch(url, {
      headers: { 'User-Agent': USER_AGENT, Accept: 'application/json' },
      signal: AbortSignal.timeout(20_000),
    });
    if (!res.ok) throw new Error(`Open-Meteo HTTP ${res.status}`);
    return readResponseJsonCapped(res, MAX_BODY_BYTES);
  }

  async function fetchLatest(lat, lon) {
    const forecastUrl =
      'https://api.open-meteo.com/v1/forecast' +
      `?latitude=${encodeURIComponent(lat)}&longitude=${encodeURIComponent(lon)}` +
      '&current=temperature_2m,relative_humidity_2m,apparent_temperature,weather_code,wind_speed_10m,wind_direction_10m,surface_pressure' +
      '&daily=temperature_2m_max,temperature_2m_min,precipitation_probability_max,weather_code' +
      '&timezone=auto&forecast_days=2';
    const qualityUrl =
      'https://air-quality-api.open-meteo.com/v1/air-quality' +
      `?latitude=${encodeURIComponent(lat)}&longitude=${encodeURIComponent(lon)}` +
      '&current=european_aqi,us_aqi,pm2_5,pm10,nitrogen_dioxide,ozone';
    const [forecast, quality] = await Promise.all([
      fetchJson(forecastUrl),
      fetchJson(qualityUrl).catch((err) => {
        // Air quality is enrichment, not the core readout: a CAMS outage
        // degrades the payload instead of failing the whole snapshot.
        console.warn(
          '[atmosphere-proxy] air quality fetch failed:',
          err?.message || err,
        );
        return null;
      }),
    ]);
    const current = forecast?.current || {};
    const daily = forecast?.daily || {};
    const air = quality?.current || null;
    if (!Number.isFinite(Number(current?.temperature_2m))) return null;
    const code = Number(current?.weather_code);
    return {
      observedAt: current?.time || null,
      timezone: forecast?.timezone || null,
      temperatureC: finite(current?.temperature_2m),
      feelsLikeC: finite(current?.apparent_temperature),
      humidityPct: finite(current?.relative_humidity_2m),
      weatherCode: Number.isFinite(code) ? code : null,
      weatherLabel: Number.isFinite(code) ? weatherLabel(code) : null,
      windKmh: finite(current?.wind_speed_10m),
      windDirDeg: finite(current?.wind_direction_10m),
      pressureHpa: finite(current?.surface_pressure),
      todayMaxC: finite(daily?.temperature_2m_max?.[0]),
      todayMinC: finite(daily?.temperature_2m_min?.[0]),
      todayPrecipPct: finite(daily?.precipitation_probability_max?.[0]),
      tomorrowMaxC: finite(daily?.temperature_2m_max?.[1]),
      tomorrowMinC: finite(daily?.temperature_2m_min?.[1]),
      tomorrowPrecipPct: finite(daily?.precipitation_probability_max?.[1]),
      europeanAqi: finite(air?.european_aqi),
      usAqi: finite(air?.us_aqi),
      pm25: finite(air?.pm2_5),
      pm10: finite(air?.pm10),
      no2: finite(air?.nitrogen_dioxide),
      ozone: finite(air?.ozone),
      airObservedAt: air?.time || null,
    };
  }

  async function resolveLatest(lat, lon) {
    const key = cellKey(lat, lon);
    const cached = mem.get(key);
    if (cached && Date.now() - cached.at < TTL_MS) {
      return { payload: cached.payload, stale: false };
    }
    if (!inflight.has(key)) {
      inflight.set(
        key,
        (async () => {
          try {
            const item = await fetchLatest(lat, lon);
            const payload = {
              fetchedAt: Date.now(),
              stale: false,
              ttlMs: TTL_MS,
              lat,
              lon,
              item,
              fallback: item
                ? null
                : { note: 'Open-Meteo unreachable — retry shortly.' },
            };
            if (item) {
              mem.set(key, { at: payload.fetchedAt, payload });
              await writeDisk();
            }
            return payload;
          } catch (err) {
            console.warn(
              '[atmosphere-proxy] fetch failed:',
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

  function makeHandler() {
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
            keyless: true,
            ttlMs: TTL_MS,
            cells: mem.size,
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
          sendJson(400, { error: 'bad_request' });
          return;
        }

        const { payload, stale } = await resolveLatest(lat, lon);
        if (payload?.item) {
          sendJson(200, { ...payload, stale });
          return;
        }
        if (payload) {
          sendJson(200, { ...payload, stale });
          return;
        }
        sendJson(502, {
          error: 'atmosphere fetch failed and no cache available',
          lat,
          lon,
        });
      } catch (err) {
        console.warn('[atmosphere-proxy] error:', err?.message || err);
        if (!res.headersSent) {
          res.writeHead(500, {
            'Content-Type': 'application/json',
            'Cache-Control': 'no-store',
          });
          res.end(JSON.stringify({ error: 'atmosphere proxy error' }));
        }
      }
    };
  }

  const installMiddleware = (server) => {
    server.middlewares.use('/api/atmosphere', makeHandler());
  };

  return {
    name: 'atmosphere-proxy',
    configureServer: installMiddleware,
    configurePreviewServer: installMiddleware,
  };
}
