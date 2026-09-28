import test from 'node:test';
import assert from 'node:assert/strict';
import * as Cesium from 'cesium';
import { createRendering } from './rendering.js';
import { createPaths } from './paths.js';
import { createModel } from './model.js';

test('a launch without a matching TLE retains its pad record but has no invented payload or orbit', () => {
  const entities = [];
  const state = {
    _missionOverlayRecords: new Map(),
    _dataSource: {
      entities: {
        add(entity) {
          entities.push(entity);
          return entity;
        },
      },
    },
    _orbitMatches: 0,
  };
  const paths = createPaths({ state });
  const model = createModel({
    state,
    parts: { policyHelpers: { parseMissionDurationSeconds: () => null } },
  });
  const rendering = createRendering({
    state,
    services: {
      satellites: {
        getSatelliteOrbitTrack: () => null,
        findSatelliteOrbitTrackInTle: () => null,
      },
    },
    parts: {
      paths,
      model,
      policyHelpers: { launchStatusAllowsOrbit: () => true },
      overlays: { missionHoverReticleImage: () => undefined },
    },
  });
  rendering.addLaunchEntity({
    id: 'unmatched',
    lat: 28,
    lon: -80,
    launchTime: new Date().toISOString(),
    orbit: { name: 'Low Earth Orbit' },
    satelliteQuery: 'Unknown Payload',
    recoveryStages: [],
    trajectory: [{ latitude: null, longitude: null }],
  });
  assert.deepEqual(
    entities.map(({ id }) => id),
    ['rocket-launch:unmatched'],
  );
  assert.equal(state._orbitMatches, 0);
  assert.equal(state._missionOverlayRecords.size, 1);
});

test('recovery endpoints require reported coordinates, including valid zero values', () => {
  const paths = createPaths({ state: {} });
  assert.equal(
    paths.landingEndpoint(
      { downrangeKm: 600, recoveryType: 'RTLS' },
      { lat: 28, lon: -80 },
      Cesium.Cartesian3.fromDegrees(-75, 30),
    ),
    null,
  );
  assert.deepEqual(paths.landingEndpoint({ lat: 0, lon: 0 }), {
    lat: 0,
    lon: 0,
    accuracy: 'SOURCE REPORTED',
  });
});

test('missing, blank and out-of-range launch coordinates cannot become a location', () => {
  const model = createModel({
    state: {},
    parts: { policyHelpers: { parseMissionDurationSeconds: () => null } },
  });
  for (const invalid of [null, undefined, '', ' ', false])
    assert.equal(model.finiteCoordinate(invalid), null);
  const net = new Date().toISOString();
  assert.deepEqual(
    model.normalizeRocketLaunches([
      { id: 'missing', net, pad: { latitude: null, longitude: null } },
      { id: 'outside', net, pad: { latitude: 91, longitude: 9 } },
    ]),
    [],
  );
  const [valid] = model.normalizeRocketLaunches([
    { id: 'valid', net, pad: { latitude: 0, longitude: 0 } },
  ]);
  assert.equal(valid.lat, 0);
  assert.equal(valid.lon, 0);
});
