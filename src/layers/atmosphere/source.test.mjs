import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createAtmosphereSource } from './source.js';

test('atmosphere source returns the proxy payload', async () => {
  const payload = {
    fetchedAt: 1,
    stale: false,
    lat: 45.4642,
    lon: 9.19,
    item: { temperatureC: 23.8, weatherLabel: 'Partly cloudy' },
  };
  const fetchImpl = async (url) => {
    assert.match(url, /\/api\/atmosphere\?lat=45\.4642&lon=9\.19/);
    return { ok: true, status: 200, json: async () => payload };
  };
  const source = createAtmosphereSource({ fetchImpl });
  assert.equal(await source.getSnapshot({ lat: 45.4642, lon: 9.19 }), payload);
});

test('atmosphere source throws on non-finite coords', async () => {
  const source = createAtmosphereSource({ fetchImpl: async () => ({}) });
  await assert.rejects(() => source.getSnapshot({ lat: NaN, lon: 9.19 }));
});

test('atmosphere source throws on hard failure without a stale payload', async () => {
  const fetchImpl = async () => ({
    ok: false,
    status: 502,
    json: async () => ({ error: 'atmosphere fetch failed' }),
  });
  const source = createAtmosphereSource({ fetchImpl });
  await assert.rejects(() => source.getSnapshot({ lat: 45.46, lon: 9.19 }));
});
