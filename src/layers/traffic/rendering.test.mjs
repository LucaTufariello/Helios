import test from 'node:test';
import assert from 'node:assert/strict';
import * as Cesium from 'cesium';
import { createRendering } from './rendering.js';
import { createState } from './state.js';
import { createFlow } from './flow.js';

function fixture(t) {
  t.mock.method(Cesium.Material, 'fromType', (type, uniforms) => ({
    uniforms,
    destroy() {},
  }));
  const lines = [];
  let renders = 0;
  const state = Object.assign(createState(), {
    _enabled: true,
    _liveMode: true,
    _flowLines: {
      removeAll() {
        lines.length = 0;
      },
      add(line) {
        lines.push(line);
      },
    },
    _viewer: {
      scene: {
        requestRender() {
          renders++;
        },
      },
    },
  });
  const rendering = createRendering({ state, parts: {} });
  const road = (level, closure = false) => ({
    type: 'primary',
    coords: [
      [9, 45],
      [9.01, 45.01],
    ],
    waypoints: [
      Cesium.Cartesian3.fromDegrees(9, 45),
      Cesium.Cartesian3.fromDegrees(9.01, 45.01),
    ],
    flow: level === undefined ? null : { level, closure },
  });
  return { state, rendering, lines, road, renders: () => renders };
}

test('only finite source flow creates road segments; uncovered roads never create vehicles', (t) => {
  const f = fixture(t);
  f.rendering.rebuildHeatLines([
    f.road(),
    f.road(NaN),
    f.road(1),
    f.road(0.7),
    f.road(0.2),
    f.road(0, true),
  ]);
  assert.equal(f.lines.length, 4);
  assert.deepEqual(f.state._bucketCounts, { free: 1, slow: 1, jam: 1 });
  assert.equal(f.state._closedRoads, 1);
  assert.equal(f.state._count, 4);
  assert.ok(f.lines.every((line) => line.positions.length === 2));
});

test('source failure removes previous flow and requests an idle-scene redraw', (t) => {
  const f = fixture(t);
  f.rendering.rebuildHeatLines([f.road(0.7)]);
  const before = f.renders();
  f.state._flowError = 'provider unavailable';
  f.rendering.rebuildHeatLines([f.road(0.7)]);
  assert.equal(f.lines.length, 0);
  assert.equal(f.state._count, 0);
  assert.ok(f.renders() > before);
});

test('the road segment cap is reported and a redraw does not invent source freshness', (t) => {
  const f = fixture(t);
  f.state._lastUpdate = 12345;
  f.rendering.renderRoadsForAltitude(
    Array.from({ length: 1202 }, () => f.road(1)),
    3000,
  );
  assert.equal(f.state._count, 1200);
  assert.equal(f.state._truncated, true);
  assert.equal(f.state._lastUpdate, 12345);
  f.rendering.rebuildHeatLines([]);
  assert.equal(f.state._truncated, false);
});

test('keyless and disabled states never draw retained source geometry', (t) => {
  const f = fixture(t);
  f.state._liveMode = false;
  f.rendering.rebuildHeatLines([f.road(1)]);
  assert.equal(f.lines.length, 0);
  f.state._liveMode = true;
  f.state._enabled = false;
  f.rendering.rebuildHeatLines([f.road(1)]);
  assert.equal(f.lines.length, 0);
});

test('terrain-supported road flow uses at most four static batches and releases them', (t) => {
  const f = fixture(t);
  t.mock.method(Cesium.RenderState, 'fromCache', () => ({})); // A unit test has no WebGL context.
  const added = [],
    removed = [];
  f.state._groundSupported = true;
  f.state._viewer.scene.groundPrimitives = {
    add(p) {
      added.push(p);
    },
    remove(p) {
      removed.push(p);
    },
  };
  f.rendering.rebuildHeatLines([
    f.road(1),
    f.road(1),
    f.road(0.7),
    f.road(0.2),
    f.road(0, true),
  ]);
  assert.equal(added.length, 4);
  assert.equal(f.lines.length, 0);
  assert.equal(f.state._count, 5);
  assert.ok(
    added.every((p) => p.classificationType === Cesium.ClassificationType.BOTH),
  );
  f.rendering.removeHeatLines();
  assert.deepEqual(removed, added);
  assert.equal(f.state._flowGroundPrimitives.length, 0);
});

test('late flow from a major-road pass cannot override newer detailed-road flow', async (t) => {
  const f = fixture(t);
  const pending = [];
  f.state._loadGeneration = 1;
  f.state._flowStatusPromise = Promise.resolve();
  const parts = { rendering: f.rendering };
  const flow = createFlow({
    state: f.state,
    parts,
    services: { credits: {} },
    source: {
      fetchFlowForBounds: () => new Promise((resolve) => pending.push(resolve)),
    },
  });
  const major = [f.road()],
    full = [f.road()];
  const a = flow.applyFlowToRoads(major, {}, 1);
  await Promise.resolve();
  const b = flow.applyFlowToRoads(full, {}, 1);
  await Promise.resolve();
  pending[1]([{ coords: full[0].coords, trafficLevel: 0.2, closure: false }]);
  await b;
  pending[0]([{ coords: major[0].coords, trafficLevel: 1, closure: false }]);
  await a;
  assert.equal(full[0].flow.level, 0.2);
  assert.equal(major[0].flow, null);
  assert.equal(f.state._flowPending, 0);
});

test('flow that settles after the paint deadline redraws only its current road set', async (t) => {
  const f = fixture(t);
  t.mock.timers.enable({ apis: ['setTimeout'] });
  f.state._loadGeneration = 1;
  f.state._flowStatusPromise = Promise.resolve();
  const pending = [];
  const flow = createFlow({
    state: f.state,
    parts: { rendering: f.rendering },
    services: { credits: {} },
    source: {
      fetchFlowForBounds: () => new Promise((resolve) => pending.push(resolve)),
    },
  });
  const flush = async () => {
    for (let i = 0; i < 8; i++) await Promise.resolve();
  };
  const roads = [f.road()];
  const first = flow.applyFlowThenRender(roads, {}, 1, 3000);
  await flush();
  t.mock.timers.tick(250);
  await first;
  assert.equal(f.lines.length, 0);
  pending[0]([{ coords: roads[0].coords, trafficLevel: 0.2, closure: false }]);
  await flush();
  assert.equal(f.lines.length, 1, 'late observed flow must become visible');
  assert.equal(f.state._bucketCounts.jam, 1);

  const oldRoads = [f.road()];
  const next = flow.applyFlowThenRender(oldRoads, {}, 1, 3000);
  await flush();
  t.mock.timers.tick(250);
  await next;
  const currentRoads = [f.road(0.7)];
  f.rendering.renderRoadsForAltitude(currentRoads, 3000);
  pending[1]([{ coords: oldRoads[0].coords, trafficLevel: 1, closure: false }]);
  await flush();
  assert.equal(f.state._roads, currentRoads);
  assert.equal(
    f.state._bucketCounts.slow,
    1,
    'retired road geometry cannot be repainted',
  );
});
