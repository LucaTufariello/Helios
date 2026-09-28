import { test } from 'node:test';
import assert from 'node:assert/strict';
import { mkdtempSync } from 'node:fs';
import { tmpdir } from 'node:os';
import path from 'node:path';
import { gdacsProxy } from './gdacs.js';

const RSS = `<?xml version="1.0" encoding="utf-8"?>
<rss version="2.0" xmlns:geo="http://www.w3.org/2003/01/geo/wgs84_pos#" xmlns:gdacs="http://www.gdacs.org" xmlns:georss="http://www.georss.org/georss">
<channel><title>GDACS RSS information</title>
<item>
<title>Red flood alert in Thailand</title>
<description>On 05/10/2026, a flood started in Thailand.</description>
<link>https://www.gdacs.org/report.aspx?eventtype=FL&amp;eventid=1104169</link>
<pubDate>Fri, 18 Sep 2026 06:16:34 GMT</pubDate>
<guid isPermaLink="false">FL1104169</guid>
<geo:Point><geo:lat>17.1053568</geo:lat><geo:long>98.9953943</geo:long></geo:Point>
</item>
<item>
<title>Orange earthquake alert in Italy</title>
<description>M5.2 offshore.</description>
<link>https://www.gdacs.org/report.aspx?eventtype=EQ&amp;eventid=1460001</link>
<pubDate>Fri, 18 Sep 2026 05:00:00 GMT</pubDate>
<guid isPermaLink="false">EQ1460001</guid>
<georss:point>40.1 15.2</georss:point>
</item>
<item>
<title>No coordinates here</title>
<description>Dropped.</description>
<link>https://www.gdacs.org/report.aspx?eventtype=DR&amp;eventid=1</link>
<pubDate>Fri, 18 Sep 2026 05:00:00 GMT</pubDate>
<guid isPermaLink="false">DR1</guid>
</item>
<item>
<title>Off-site link</title>
<description>Dropped: link must stay on gdacs.org.</description>
<link>https://evil.example/report</link>
<pubDate>Fri, 18 Sep 2026 05:00:00 GMT</pubDate>
<guid isPermaLink="false">FL2</guid>
<geo:Point><geo:lat>10</geo:lat><geo:long>20</geo:long></geo:Point>
</item>
</channel></rss>`;

function install(fetchImpl) {
  const realFetch = globalThis.fetch;
  globalThis.fetch = fetchImpl;
  return () => {
    globalThis.fetch = realFetch;
  };
}

function handlerFor(fetchImpl) {
  const routes = new Map();
  const plugin = gdacsProxy({
    cacheDir: mkdtempSync(path.join(tmpdir(), 'gev-gdacs-test-')),
  });
  plugin.configureServer({
    middlewares: { use: (route, fn) => routes.set(route, fn) },
  });
  return routes.get('/api/gdacs');
}

function req(url = '/') {
  return { url, socket: { remoteAddress: '127.0.0.1' } };
}

function resCapture() {
  return {
    status: null,
    body: null,
    headersSent: false,
    writeHead(status) {
      this.status = status;
    },
    end(body) {
      this.body = body;
      this.headersSent = true;
    },
  };
}

const okFetch = async () => ({
  ok: true,
  status: 200,
  headers: { get: () => 'application/rss+xml' },
  text: async () => RSS,
});

test('gdacs handler parses geo alerts and drops the rest', async () => {
  const restore = install(okFetch);
  try {
    const handler = handlerFor(okFetch);
    const res = resCapture();
    await handler(req('/'), res);
    assert.equal(res.status, 200);
    const payload = JSON.parse(res.body);
    assert.equal(payload.count, 2);
    assert.equal(payload.alerts[0].severity, 'Red');
    assert.equal(payload.alerts[0].type, 'FL');
    assert.equal(payload.alerts[0].lat, 17.1053568);
    assert.equal(payload.alerts[0].id, 'FL1104169');
    assert.equal(
      new URL(payload.alerts[0].link).searchParams.get('eventid'),
      '1104169',
    );
    assert.ok(
      !payload.alerts.some((alert) => alert.lat === 0 && alert.lon === 0),
    );
    assert.equal(payload.alerts[1].severity, 'Orange');
    assert.equal(payload.alerts[1].type, 'EQ');
    assert.equal(payload.alerts[1].lat, 40.1);
    assert.equal(payload.alerts[1].lon, 15.2);
  } finally {
    restore();
  }
});

test('gdacs status reports the cached count', async () => {
  const restore = install(okFetch);
  try {
    const routes = new Map();
    const plugin = gdacsProxy({
      cacheDir: mkdtempSync(path.join(tmpdir(), 'gev-gdacs-test-')),
    });
    plugin.configureServer({
      middlewares: { use: (route, fn) => routes.set(route, fn) },
    });
    const handler = routes.get('/api/gdacs');
    const first = resCapture();
    await handler(req('/status'), first);
    assert.equal(first.status, 200);
    assert.equal(JSON.parse(first.body).count, 0);
    const second = resCapture();
    await handler(req('/'), second);
    assert.equal(second.status, 200);
    const third = resCapture();
    await handler(req('/status'), third);
    assert.equal(JSON.parse(third.body).count, 2);
  } finally {
    restore();
  }
});

test('gdacs answers 502 with no cache on upstream failure', async () => {
  const restore = install(async () => {
    throw new Error('downstream down');
  });
  try {
    const handler = handlerFor(null);
    const res = resCapture();
    await handler(req('/'), res);
    assert.equal(res.status, 502);
  } finally {
    restore();
  }
});
