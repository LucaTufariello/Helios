import { test } from 'node:test';
import assert from 'node:assert/strict';
import {
  ENERGY_ELEMENT_CAP,
  createEnergySource,
  normalizeEnergyElement,
} from './source.js';

test('normalize maps nodes and way centers to kinds', () => {
  assert.deepEqual(
    normalizeEnergyElement({
      type: 'node',
      id: 1,
      lat: 45.5,
      lon: 9.2,
      tags: { power: 'plant', name: 'Centrale di Test' },
    }),
    {
      id: 'node/1',
      kind: 'plant',
      name: 'Centrale di Test',
      lat: 45.5,
      lon: 9.2,
    },
  );
  assert.equal(
    normalizeEnergyElement({
      type: 'way',
      id: 2,
      center: { lat: 45.6, lon: 9.3 },
      tags: { industrial: 'refinery' },
    }).kind,
    'refinery',
  );
  assert.equal(
    normalizeEnergyElement({
      type: 'node',
      id: 3,
      lat: 45.5,
      lon: 9.2,
      tags: { power: 'substation' },
    }).name,
    null,
  );
  assert.equal(
    normalizeEnergyElement({
      type: 'node',
      id: 4,
      lat: 45.5,
      lon: 9.2,
      tags: { amenity: 'cafe' },
    }),
    null,
  );
  assert.equal(
    normalizeEnergyElement({ type: 'node', id: 5, tags: { power: 'plant' } }),
    null,
  );
});

test('source posts a bounded query and caps elements', async () => {
  let body = '';
  const elements = Array.from({ length: ENERGY_ELEMENT_CAP + 50 }, (_, i) => ({
    type: 'node',
    id: i,
    lat: 45.5,
    lon: 9.2,
    tags: { power: 'substation' },
  }));
  const fetchImpl = async (url, options) => {
    assert.equal(url, '/api/overpass');
    assert.equal(options.method, 'POST');
    body = options.body;
    return { ok: true, status: 200, json: async () => ({ elements }) };
  };
  const source = createEnergySource({ fetchImpl });
  const box = { south: 45.3, west: 9.0, north: 45.6, east: 9.4 };
  const { sites, saturated } = await source.getSites(box, {});
  assert.equal(sites.length, ENERGY_ELEMENT_CAP);
  assert.equal(saturated, true);
  assert.match(body, /power/);
  assert.match(decodeURIComponent(body), /45\.3,9,45\.6,9\.4/);
});

test('source rejects oversized viewports', async () => {
  const source = createEnergySource({ fetchImpl: async () => ({}) });
  await assert.rejects(() =>
    source.getSites({ south: 40, west: 5, north: 50, east: 15 }, {}),
  );
});

test('source throws on malformed payloads', async () => {
  const fetchImpl = async () => ({
    ok: true,
    status: 200,
    json: async () => ({}),
  });
  const source = createEnergySource({ fetchImpl });
  await assert.rejects(() =>
    source.getSites({ south: 45.3, west: 9.0, north: 45.6, east: 9.4 }, {}),
  );
});
