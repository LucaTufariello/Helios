import { test } from 'node:test';
import assert from 'node:assert/strict';
import {
  escapeSentinelText,
  sentinelDetailsFromSnapshot,
  sentinelKindLabel,
  sentinelSectionHtml,
} from './panel.js';

function s2Snapshot() {
  return {
    fetchedAt: Date.parse('2026-09-17T08:00:00.000Z'),
    stale: false,
    kind: 's2',
    lat: 45.4642,
    lon: 9.19,
    item: {
      kind: 's2',
      datetime: '2026-08-30T10:20:41.024000Z',
      cloudCover: 3.6,
      bbox: [9.0, 45.0, 10.4, 46.0],
      collection: 'SENTINEL-2',
      name: 'S2B_MSIL2A_20260830T101559_N0512_R065_T32TNR_20260830T144520.SAFE',
      browserUrl: 'https://browser.dataspace.copernicus.eu/?zoom=12',
      eoBrowserUrl: 'https://apps.sentinel-hub.com/eo-browser/?zoom=12',
    },
    fallback: null,
  };
}

test('kind labels distinguish optical from radar', () => {
  assert.match(sentinelKindLabel('s2'), /SENTINEL-2 OPTICAL/);
  assert.match(sentinelKindLabel('s1'), /RADAR, NOT PHOTO/);
});

test('details normalize a full S2 snapshot', () => {
  const details = sentinelDetailsFromSnapshot(s2Snapshot(), 's2', {
    updatedAt: Date.parse('2026-09-17T08:01:00.000Z'),
  });
  assert.equal(details.hasItem, true);
  assert.equal(details.dateLabel, '2026-08-30');
  assert.equal(details.cloudLabel, '3.6%');
  assert.match(details.pointLabel, /45\.4642.*N/);
  assert.match(details.pointLabel, /9\.1900.*E/);
  assert.ok(details.bboxLabel.includes('9.00'));
  assert.ok(details.browserUrl.includes('browser.dataspace.copernicus.eu'));
  assert.equal(details.error, null);
});

test('S1 details report no cloud concept', () => {
  const snapshot = s2Snapshot();
  snapshot.kind = 's1';
  snapshot.item.kind = 's1';
  snapshot.item.cloudCover = null;
  const details = sentinelDetailsFromSnapshot(snapshot, 's1', {});
  assert.match(details.kindLabel, /RADAR/);
  assert.equal(details.cloudLabel, 'N/A — RADAR');
});

test('fallback snapshot carries the note and link-outs without an item', () => {
  const details = sentinelDetailsFromSnapshot(
    {
      kind: 's2',
      lat: 45.4642,
      lon: 9.19,
      item: null,
      fallback: {
        note: 'No recent item in the OData catalogue — try the browser link-outs.',
        browserUrl: 'https://browser.dataspace.copernicus.eu/?zoom=12',
        eoBrowserUrl: 'https://apps.sentinel-hub.com/eo-browser/?zoom=12',
      },
    },
    's2',
    {},
  );
  assert.equal(details.hasItem, false);
  assert.match(details.fallbackNote, /No recent item/);
  assert.ok(details.browserUrl);
  const html = sentinelSectionHtml(details);
  assert.match(html, /No recent item/);
  assert.match(html, /COPERNICUS BROWSER/);
  assert.match(html, /EO BROWSER/);
});

test('empty snapshot renders the loading state', () => {
  const details = sentinelDetailsFromSnapshot(null, 's2', {});
  assert.equal(details.hasItem, false);
  assert.match(sentinelSectionHtml(details), /LOADING LATEST SCENE/);
});

test('error without an item renders the message, not a crash', () => {
  const details = sentinelDetailsFromSnapshot(null, 's2', {
    error: 'Sentinel HTTP 502',
  });
  assert.match(sentinelSectionHtml(details), /SOURCE ERROR/);
  assert.match(sentinelSectionHtml(details), /Sentinel HTTP 502/);
});

test('section HTML escapes hostile product names', () => {
  const snapshot = s2Snapshot();
  snapshot.item.name = '<script>alert(1)</script>.SAFE';
  const html = sentinelSectionHtml(
    sentinelDetailsFromSnapshot(snapshot, 's2', {}),
  );
  assert.doesNotMatch(html, /<script>/);
  assert.match(html, /&lt;script&gt;/);
  assert.match(html, /2026-08-30T10:20:41/);
  assert.match(html, /3\.6%/);
});

test('escape helper covers quotes for attributes', () => {
  assert.equal(escapeSentinelText('a"b\'c&d'), 'a&quot;b&#39;c&amp;d');
});
