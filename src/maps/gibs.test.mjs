import { test } from 'node:test';
import assert from 'node:assert/strict';
import {
  GIBS_LAYERS,
  gibsDateString,
  gibsUrlTemplate,
} from '../data/gibsLayers.js';

test('gibs date defaults to yesterday (UTC)', () => {
  const at = new Date('2026-09-17T12:00:00Z');
  assert.equal(gibsDateString(at, 1), '2026-09-16');
});

test('gibs url template orders TileMatrix/TileRow/TileCol as z/y/x', () => {
  const url = gibsUrlTemplate(GIBS_LAYERS.trueColor.id, '2026-09-16', 'jpg');
  assert.ok(url.includes('epsg3857/best/'));
  assert.ok(url.includes('VIIRS_SNPP_CorrectedReflectance_TrueColor'));
  assert.ok(url.includes('2026-09-16'));
  assert.ok(url.includes('GoogleMapsCompatible_Level9/{z}/{y}/{x}.jpg'));
});
