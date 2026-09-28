import path from 'node:path';
import { promises as fsp } from 'node:fs';

import { readResponseTextCapped } from './common/http.js';
import { makeRateLimiter, clientKey } from './common/rate-limit.js';

/**
 * Free disaster-alerts proxy (public domain, no key).
 *
 * Upstream: GDACS RSS (https://www.gdacs.org/xml/rss.xml, UN/EU JRC,
 * explicitly public domain, ~350 current alerts with geo coordinates).
 * Fixed upstream host (no SSRF surface): the client sends no parameters.
 * XML is parsed with the same defensive tag-extraction pattern as the
 * regional news provider — no XML dependency, every field validated.
 *
 * Routes:
 *   GET /api/gdacs → current alerts as JSON
 *   GET /api/gdacs/status
 *
 * Budgets: 60/min per-IP, 2MB response cap, identifying User-Agent,
 * 30min mem cache + disk under `.gev-cache/gdacs.json`.
 *
 * @returns {import('vite').Plugin}
 */
export function gdacsProxy({ cacheDir } = {}) {
  const TTL_MS = 30 * 60_000;
  const MAX_BODY_BYTES = 2 * 1024 * 1024;
  const CACHE_DIR = cacheDir || path.join(process.cwd(), '.gev-cache');
  const USER_AGENT = 'gods-eye-view-gdacs-proxy/1.0';
  const FEED_URL = 'https://www.gdacs.org/xml/rss.xml';

  const limiter = makeRateLimiter({
    windowMs: 60_000,
    max: 60,
    globalMax: 200,
  });

  let mem = null;
  let inflight = null;
  let diskChecked = false;

  async function readDiskOnce() {
    if (diskChecked) return;
    diskChecked = true;
    try {
      const parsed = JSON.parse(
        await fsp.readFile(path.join(CACHE_DIR, 'gdacs.json'), 'utf8'),
      );
      if (parsed?.schemaVersion === 2 && Array.isArray(parsed.alerts))
        mem = parsed;
    } catch {
      /* no disk cache yet */
    }
  }

  async function writeDisk(payload) {
    try {
      await fsp.mkdir(CACHE_DIR, { recursive: true });
      await fsp.writeFile(
        path.join(CACHE_DIR, 'gdacs.json'),
        JSON.stringify(payload),
        'utf8',
      );
    } catch (err) {
      console.warn('[gdacs-proxy] cache write failed:', err?.message || err);
    }
  }

  /** Extract one namespaced tag's text from an <item> block. */
  function rssTag(block, tag) {
    const match = String(block || '').match(
      new RegExp(`<${tag}(?:\\s[^>]*)?>([\\s\\S]*?)</${tag}>`, 'i'),
    );
    return (match?.[1] || '')
      .replace(/<!\[CDATA\[([\s\S]*?)\]\]>/g, '$1')
      .replace(/&amp;/g, '&')
      .replace(/&quot;/g, '"')
      .replace(/&#39;/g, "'")
      .replace(/&lt;/g, '<')
      .replace(/&gt;/g, '>')
      .trim();
  }

  function finite(value) {
    if (
      value === null ||
      value === undefined ||
      typeof value === 'boolean' ||
      String(value).trim() === ''
    )
      return null;
    const number = Number(value);
    return Number.isFinite(number) ? number : null;
  }

  /** Severity word leading the title ("Green flood alert …"), or null. */
  function severityOf(title) {
    const match = String(title || '').match(/^(Green|Orange|Red)\b/i);
    return match
      ? match[1][0].toUpperCase() + match[1].slice(1).toLowerCase()
      : null;
  }

  /** Event family from the guid prefix (FL1104169) or link eventtype. */
  function typeOf(item, link) {
    const guid = rssTag(item, 'guid');
    const fromGuid = String(guid || '').match(/^(FL|EQ|TC|VO|DR|WF)/i);
    if (fromGuid) return fromGuid[1].toUpperCase();
    const fromLink = String(link || '').match(/eventtype=([A-Z]{2})/i);
    return fromLink ? fromLink[1].toUpperCase() : null;
  }

  function coordinatesOf(item) {
    const lat = finite(rssTag(item, 'geo:lat'));
    const lon = finite(rssTag(item, 'geo:long'));
    if (lat !== null && lon !== null) return { lat, lon };
    // Fallback: <georss:point>lat lon</georss:point>.
    const point = rssTag(item, 'georss:point').split(/\s+/).map(Number);
    if (
      point.length === 2 &&
      Number.isFinite(point[0]) &&
      Number.isFinite(point[1])
    )
      return { lat: point[0], lon: point[1] };
    return null;
  }

  function normalizeItem(item) {
    const title = rssTag(item, 'title').slice(0, 180);
    const rawLink = rssTag(item, 'link');
    let link = null;
    try {
      const parsed = new URL(rawLink);
      if (
        (parsed.protocol === 'http:' || parsed.protocol === 'https:') &&
        parsed.hostname === 'www.gdacs.org'
      )
        link = parsed.href;
    } catch {
      /* unusable link */
    }
    const coords = coordinatesOf(item);
    if (!title || !link || !coords) return null;
    if (Math.abs(coords.lat) > 90 || Math.abs(coords.lon) > 180) return null;
    const rawDate = rssTag(item, 'pubDate');
    return {
      id: rssTag(item, 'guid').slice(0, 40) || link,
      title,
      description: rssTag(item, 'description').slice(0, 280) || null,
      link,
      severity: severityOf(title),
      type: typeOf(item, link),
      lat: coords.lat,
      lon: coords.lon,
      publishedAt: Number.isNaN(Date.parse(rawDate))
        ? null
        : new Date(rawDate).toISOString(),
    };
  }

  function normalizeFeed(xml) {
    const alerts = [];
    for (const match of String(xml || '').matchAll(
      /<item>([\s\S]*?)<\/item>/gi,
    )) {
      const alert = normalizeItem(match[1]);
      if (alert) alerts.push(alert);
    }
    return alerts;
  }

  async function fetchLatest() {
    const controller = new AbortController();
    const timeoutId = setTimeout(() => controller.abort(), 20_000);
    try {
      const res = await fetch(FEED_URL, {
        // NOTE: gdacs.org answers 406 to `Accept: application/rss+xml`
        // (Apache content negotiation) — `*/*` returns the same feed as 200.
        headers: { 'User-Agent': USER_AGENT, Accept: '*/*' },
        signal: controller.signal,
      });
      if (!res.ok) throw new Error(`GDACS HTTP ${res.status}`);
      const xml = await readResponseTextCapped(
        res,
        MAX_BODY_BYTES,
        controller.signal,
      );
      const alerts = normalizeFeed(xml);
      return {
        schemaVersion: 2,
        fetchedAt: Date.now(),
        stale: false,
        ttlMs: TTL_MS,
        count: alerts.length,
        alerts,
      };
    } finally {
      clearTimeout(timeoutId);
    }
  }

  async function resolveLatest() {
    if (mem && Date.now() - mem.fetchedAt < TTL_MS) {
      return { payload: mem, stale: false };
    }
    if (!inflight) {
      inflight = (async () => {
        try {
          const payload = await fetchLatest();
          mem = payload;
          await writeDisk(payload);
          return payload;
        } catch (err) {
          console.warn('[gdacs-proxy] fetch failed:', err?.message || err);
          return null;
        } finally {
          inflight = null;
        }
      })();
    }
    const fresh = await inflight;
    if (fresh) return { payload: fresh, stale: false };
    if (mem) return { payload: { ...mem, stale: true }, stale: true };
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
            count: mem?.alerts?.length ?? 0,
          });
          return;
        }

        const { payload, stale } = await resolveLatest();
        if (payload) {
          sendJson(200, { ...payload, stale });
          return;
        }
        sendJson(502, {
          error: 'gdacs fetch failed and no cache available',
        });
      } catch (err) {
        console.warn('[gdacs-proxy] error:', err?.message || err);
        if (!res.headersSent) {
          res.writeHead(500, {
            'Content-Type': 'application/json',
            'Cache-Control': 'no-store',
          });
          res.end(JSON.stringify({ error: 'gdacs proxy error' }));
        }
      }
    };
  }

  const installMiddleware = (server) => {
    server.middlewares.use('/api/gdacs', makeHandler());
  };

  return {
    name: 'gdacs-proxy',
    configureServer: installMiddleware,
    configurePreviewServer: installMiddleware,
  };
}
