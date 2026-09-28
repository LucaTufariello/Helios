import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createGdacsSource } from './source.js';

test('gdacs source returns the alert payload', async () => {
  const payload = {
    fetchedAt: 1,
    stale: false,
    count: 1,
    alerts: [{ id: 'FL1', title: 'Red flood alert', lat: 10, lon: 20 }],
  };
  const fetchImpl = async (url) => {
    assert.equal(url, '/api/gdacs');
    return { ok: true, status: 200, json: async () => payload };
  };
  const source = createGdacsSource({ fetchImpl });
  assert.equal(await source.getSnapshot({}), payload);
});

test('gdacs source throws on hard failure', async () => {
  const fetchImpl = async () => ({
    ok: false,
    status: 502,
    json: async () => ({ error: 'gdacs fetch failed' }),
  });
  const source = createGdacsSource({ fetchImpl });
  await assert.rejects(() => source.getSnapshot({}));
});
