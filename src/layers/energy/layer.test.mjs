import { test } from 'node:test';
import assert from 'node:assert/strict';
import * as Cesium from 'cesium';
import { createEnergyLayer, energyViewportBox } from './index.js';

function fakeViewer() {
  return {
    dataSources: { add() {}, remove() {} },
    scene: { globe: { ellipsoid: Cesium.Ellipsoid.WGS84 } },
    camera: {},
  };
}

const overlayHost = {
  setEntries() {},
  setVisible() {},
  clearSource() {},
};

test('refresh renders sites from an explicit viewport', async () => {
  const source = {
    async getSites(box) {
      assert.deepEqual(box, { south: 45.3, west: 9.0, north: 45.6, east: 9.4 });
      return {
        sites: [
          {
            id: 'node/1',
            kind: 'plant',
            name: 'Centrale',
            lat: 45.5,
            lon: 9.2,
          },
          {
            id: 'node/2',
            kind: 'substation',
            name: null,
            lat: 45.51,
            lon: 9.21,
          },
        ],
        saturated: false,
      };
    },
  };
  const viewer = fakeViewer();
  const layer = createEnergyLayer({ source, overlayHost });
  layer.init(viewer);
  layer.enable();
  const updated = await layer.refresh({
    south: 45.3,
    west: 9.0,
    north: 45.6,
    east: 9.4,
  });
  assert.equal(updated, true);
  const stats = layer.getStats();
  assert.equal(stats.count, 2);
  assert.match(stats.coverage, /1 plants/);
  assert.match(stats.coverage, /1 substations/);
  const records = layer.getAnalystRecords();
  assert.equal(records.length, 2);
  assert.equal(records[0].kind, 'plant');
  layer.destroy();
});

test('global views stay enabled without querying', async () => {
  let queried = 0;
  const source = {
    async getSites() {
      queried += 1;
      return { sites: [], saturated: false };
    },
  };
  const viewer = fakeViewer();
  viewer.camera.computeViewRectangle = () => null; // globe view
  const layer = createEnergyLayer({ source, overlayHost });
  layer.init(viewer);
  layer.enable();
  const updated = await layer.update();
  assert.equal(updated, true);
  assert.equal(queried, 0);
  layer.destroy();
});

test('energyViewportBox clamps dense spans', () => {
  assert.equal(energyViewportBox({ camera: {}, scene: {} }), null);
});
