import { test } from 'node:test';
import assert from 'node:assert/strict';
import {
  isNight,
  subsolarPoint,
  sunElevation,
  terminatorPositions,
} from './terminator.js';

test('subsolar point stays in valid ranges', () => {
  const sub = subsolarPoint(new Date('2026-09-17T12:00:00Z'));
  assert.ok(sub.lat >= -23.5 && sub.lat <= 23.5);
  assert.ok(sub.lon >= -180 && sub.lon <= 180);
});

test('sun elevation is positive at subsolar point, negative antipodal', () => {
  const at = new Date('2026-09-17T12:00:00Z');
  const sub = subsolarPoint(at);
  assert.ok(sunElevation(sub.lat, sub.lon, at) > 80);
  assert.ok(
    isNight(-sub.lat, sub.lon + 180 > 180 ? sub.lon - 180 : sub.lon + 180, at),
  );
});

test('terminator forms a full longitude ring', () => {
  const positions = terminatorPositions(new Date('2026-09-17T12:00:00Z'), 10);
  assert.ok(positions.length >= 30);
  for (const { lat, lon } of positions) {
    assert.ok(lat >= -90 && lat <= 90);
    assert.ok(lon >= -180 && lon <= 180);
  }
});
