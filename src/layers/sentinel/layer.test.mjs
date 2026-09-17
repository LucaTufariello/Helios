import { test } from 'node:test';
import assert from 'node:assert/strict';
import * as Cesium from 'cesium';
import { createSentinelLayer } from './index.js';

function fakeViewer() {
  return {
    dataSources: { add() {}, remove() {} },
    creditDisplay: { addStaticCredit() {} },
    entities: { add: (entity) => entity, remove() {} },
    scene: {
      canvas: { clientWidth: 800, clientHeight: 600 },
      globe: {
        pick: () => Cesium.Cartesian3.fromDegrees(9.19, 45.4642),
      },
    },
    camera: { getPickRay: () => ({}) },
  };
}

const overlayHost = {
  entries: [],
  setEntries() {},
  setVisible() {},
  clearSource() {},
};

function payload() {
  return {
    fetchedAt: Date.now(),
    stale: false,
    ttlMs: 3_600_000,
    kind: 's2',
    lat: 45.4642,
    lon: 9.19,
    item: {
      kind: 's2',
      datetime: '2026-08-30T10:20:41.024000Z',
      cloudCover: 3.6,
      bbox: [9, 45, 10.4, 46],
      name: 'S2A_MSIL2A_test.SAFE',
      browserUrl: 'https://browser.dataspace.copernicus.eu/',
      eoBrowserUrl: 'https://apps.sentinel-hub.com/eo-browser/',
    },
  };
}

test('manager-shaped update(viewer, { signal }) queries finite camera coords', async () => {
  const calls = [];
  const source = {
    async getSnapshot(args) {
      calls.push(args);
      return payload();
    },
  };
  const viewer = fakeViewer();
  const layer = createSentinelLayer({ source, overlayHost });
  layer.init(viewer);
  layer.enable();
  // Manager contract: second arg is lifecycle options, never a viewport.
  const updated = await layer.update(viewer, {
    signal: new AbortController().signal,
  });
  assert.equal(updated, true);
  assert.ok(calls.length >= 1, 'source should have been queried');
  for (const call of calls) {
    assert.ok(Number.isFinite(call.lat), `lat finite, got ${call.lat}`);
    assert.ok(Number.isFinite(call.lon), `lon finite, got ${call.lon}`);
  }
  const stats = layer.getStats();
  assert.equal(stats.count, 1);
  assert.match(stats.coverage, /S2 10m/);
  const details = layer.getDetails();
  assert.equal(details.hasItem, true);
  assert.equal(details.dateLabel, '2026-08-30');
  assert.equal(details.cloudLabel, '3.6%');
  assert.ok(details.browserUrl.includes('browser.dataspace.copernicus.eu'));
  layer.destroy(viewer);
});

test('details before any snapshot report the loading state', () => {
  const layer = createSentinelLayer({
    source: {
      async getSnapshot() {
        throw new Error('unreachable');
      },
    },
    overlayHost,
  });
  const details = layer.getDetails();
  assert.equal(details.hasItem, false);
  assert.equal(details.error, null);
  layer.destroy();
});

test('sky view (no ground center) stays enabled without querying', async () => {
  let queried = 0;
  const source = {
    async getSnapshot() {
      queried += 1;
      return payload();
    },
  };
  const viewer = fakeViewer();
  viewer.scene.globe.pick = () => null; // looking at sky/horizon
  const layer = createSentinelLayer({ source, overlayHost });
  layer.init(viewer);
  layer.enable();
  const updated = await layer.update(viewer, {
    signal: new AbortController().signal,
  });
  assert.equal(updated, true);
  assert.equal(queried, 0);
  layer.destroy(viewer);
});
