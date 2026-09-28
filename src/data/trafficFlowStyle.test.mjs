// src/data/trafficFlowStyle.test.mjs
import { test } from 'node:test';
import assert from 'node:assert/strict';
import {
  flowBucket,
  flowColor,
  FLOW_BUCKET_RGBA,
} from './trafficFlowStyle.js';

// ── bucket thresholds ───────────────────────────────────────

test('flowBucket: >= 0.85 is free-flowing', () => {
  assert.equal(flowBucket(1), 'free');
  assert.equal(flowBucket(0.85), 'free');
  assert.equal(flowBucket(0.9), 'free');
});

test('flowBucket: [0.55, 0.85) is slow', () => {
  assert.equal(flowBucket(0.849999), 'slow');
  assert.equal(flowBucket(0.7), 'slow');
  assert.equal(flowBucket(0.55), 'slow');
});

test('flowBucket: < 0.55 is jammed', () => {
  assert.equal(flowBucket(0.549999), 'jam');
  assert.equal(flowBucket(0.2), 'jam');
  assert.equal(flowBucket(0), 'jam');
});

test('flowBucket: non-finite input degrades to free (no false alarms)', () => {
  assert.equal(flowBucket(NaN), 'free');
  assert.equal(flowBucket(undefined), 'free');
});

// ── colors ──────────────────────────────────────────────────

test('flowColor returns the rgba tuple for the level bucket', () => {
  assert.deepEqual(flowColor(1), FLOW_BUCKET_RGBA.free);
  assert.deepEqual(flowColor(0.7), FLOW_BUCKET_RGBA.slow);
  assert.deepEqual(flowColor(0.1), FLOW_BUCKET_RGBA.jam);
});

test('bucket palette: green #2ecc71 / amber #f0b23e / red #e05252 @ alpha 0.9', () => {
  assert.deepEqual(FLOW_BUCKET_RGBA.free, [46, 204, 113, 0.9]);
  assert.deepEqual(FLOW_BUCKET_RGBA.slow, [240, 178, 62, 0.9]);
  assert.deepEqual(FLOW_BUCKET_RGBA.jam, [224, 82, 82, 0.9]);
});
