import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createGdacsLayer } from './index.js';

function fakeViewer() {
  return { dataSources: { add() {}, remove() {} } };
}

const overlayHost = {
  setEntries() {},
  setVisible() {},
  clearSource() {},
};

test('refresh renders alerts with severity stats', async () => {
  const source = {
    async getSnapshot() {
      return {
        fetchedAt: Date.now(),
        stale: false,
        count: 3,
        alerts: [
          {
            id: 'FL1',
            title: 'Red flood alert',
            severity: 'Red',
            type: 'FL',
            lat: 10,
            lon: 20,
            link: 'https://www.gdacs.org/x',
          },
          {
            id: 'EQ1',
            title: 'Orange earthquake alert',
            severity: 'Orange',
            type: 'EQ',
            lat: 40,
            lon: 15,
            link: 'https://www.gdacs.org/y',
          },
          {
            id: 'FL2',
            title: 'Green flood alert',
            severity: 'Green',
            type: 'FL',
            lat: -10,
            lon: -20,
            link: 'https://www.gdacs.org/z',
          },
        ],
      };
    },
  };
  const viewer = fakeViewer();
  const layer = createGdacsLayer({ source, overlayHost });
  layer.init(viewer);
  layer.enable();
  const updated = await layer.update();
  assert.equal(updated, true);
  const stats = layer.getStats();
  assert.equal(stats.count, 3);
  assert.match(stats.coverage, /1 Red/);
  assert.match(stats.coverage, /1 Orange/);
  assert.match(stats.coverage, /1 Green/);
  const records = layer.getAnalystRecords();
  assert.equal(records.length, 3);
  assert.equal(records[0].severity, 'Red');
  layer.destroy();
});

test('alerts without coordinates never reach the scene', async () => {
  const source = {
    async getSnapshot() {
      return {
        fetchedAt: Date.now(),
        stale: false,
        count: 2,
        alerts: [
          {
            id: 'FL1',
            title: 'Nowhere alert',
            severity: 'Red',
            type: 'FL',
            lat: null,
            lon: null,
          },
        ],
      };
    },
  };
  const viewer = fakeViewer();
  const layer = createGdacsLayer({ source, overlayHost });
  layer.init(viewer);
  layer.enable();
  assert.equal(await layer.update(), true);
  assert.equal(layer.getAnalystRecords().length, 0);
  layer.destroy();
});
