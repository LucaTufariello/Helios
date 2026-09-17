import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createSentinelSource, sentinelBrowserUrls } from './source.js';

test('sentinel source returns fallback descriptor on 400', async () => {
  const fetchImpl = async () => ({
    ok: false,
    status: 400,
    json: async () => ({ fallback: { browserUrl: 'https://example.test/a' } }),
  });
  const source = createSentinelSource({ fetchImpl });
  const snapshot = await source.getSnapshot({ lat: 45.46, lon: 9.19 });
  assert.equal(snapshot.fallback, true);
});

test('sentinel source throws on non-finite coords', async () => {
  const source = createSentinelSource({ fetchImpl: async () => ({}) });
  await assert.rejects(() => source.getSnapshot({ lat: NaN, lon: 9 }));
});

test('sentinel browser urls carry the viewport', () => {
  const urls = sentinelBrowserUrls(45.46, 9.19);
  assert.ok(urls.copernicus.includes('45.46'));
  assert.ok(urls.eoBrowser.includes('9.19'));
});
