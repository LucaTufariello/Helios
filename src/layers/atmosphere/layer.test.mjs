import { test } from 'node:test';
import assert from 'node:assert/strict';
import * as Cesium from 'cesium';
import { createAtmosphereLayer } from './index.js';

function fakeViewer() {
  return {
    creditDisplay: { addStaticCredit() {} },
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
    ttlMs: 900_000,
    lat: 45.4642,
    lon: 9.19,
    item: {
      observedAt: '2026-09-18T17:30',
      temperatureC: 23.8,
      feelsLikeC: 24.9,
      humidityPct: 59,
      weatherCode: 2,
      weatherLabel: 'Partly cloudy',
      windKmh: 4.3,
      windDirDeg: 42,
      pressureHpa: 997.3,
      europeanAqi: 52,
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
  const layer = createAtmosphereLayer({ source, overlayHost });
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
  assert.match(stats.coverage, /Partly cloudy/);
  assert.match(stats.coverage, /EAQI 52/);
  const details = layer.getDetails();
  assert.equal(details.hasItem, true);
  assert.equal(details.temperatureLabel, '23.8°C');
  assert.equal(details.aqiLabel, '52 · Moderate');
  const records = layer.getAnalystRecords();
  assert.equal(records.length, 1);
  assert.equal(records[0].temperatureC, 23.8);
  layer.destroy(viewer);
});

test('details before any snapshot report the loading state', () => {
  const layer = createAtmosphereLayer({
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
  const layer = createAtmosphereLayer({ source, overlayHost });
  layer.init(viewer);
  layer.enable();
  const updated = await layer.update(viewer, {
    signal: new AbortController().signal,
  });
  assert.equal(updated, true);
  assert.equal(queried, 0);
  layer.destroy(viewer);
});
