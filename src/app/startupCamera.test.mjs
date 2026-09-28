import test from 'node:test';
import assert from 'node:assert/strict';
import { CAMERA_PRESETS, flyToAustin, flyToMilan } from '../camera.js';

test('teardown before the initial camera delay prevents a late flight', (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  let flights = 0;
  let cancelled = 0;
  const stop = flyToAustin({
    isDestroyed: () => false,
    camera: {
      setView() {},
      flyTo() {
        flights++;
      },
      cancelFlight() {
        cancelled++;
      },
    },
  });
  stop();
  t.mock.timers.tick(1000);
  assert.equal(flights, 0);
  assert.equal(cancelled, 1);
});

test('fresh boots fly to Milan, and Milan teardown also prevents a late flight', (t) => {
  t.mock.timers.enable({ apis: ['setTimeout'] });
  assert.ok(CAMERA_PRESETS.milan, 'a Milan preset exists');
  let flights = 0;
  let cancelled = 0;
  const viewer = {
    isDestroyed: () => false,
    camera: {
      setView() {},
      flyTo() {
        flights++;
      },
      cancelFlight() {
        cancelled++;
      },
    },
  };
  const stop = flyToMilan(viewer);
  stop();
  t.mock.timers.tick(1000);
  assert.equal(flights, 0);
  assert.equal(cancelled, 1);
});
