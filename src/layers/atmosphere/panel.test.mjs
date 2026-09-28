import { test } from 'node:test';
import assert from 'node:assert/strict';
import {
  atmosphereDetailsFromSnapshot,
  atmosphereSectionHtml,
  compassLabel,
  escapeAtmosphereText,
  europeanAqiBand,
} from './panel.js';

function milanSnapshot() {
  return {
    fetchedAt: Date.parse('2026-09-18T17:30:00.000Z'),
    stale: false,
    lat: 45.4642,
    lon: 9.19,
    item: {
      observedAt: '2026-09-18T17:30',
      temperatureC: 23.8,
      feelsLikeC: 24.9,
      humidityPct: 59,
      weatherCode: 2,
      weatherLabel: 'Partly cloudy',
      windKmh: 4.3,
      windDirDeg: 42,
      pressureHpa: 997.3,
      todayMaxC: 26.1,
      todayMinC: 17.4,
      todayPrecipPct: 10,
      tomorrowMaxC: 24.8,
      tomorrowMinC: 16.9,
      tomorrowPrecipPct: 20,
      europeanAqi: 52,
      pm25: 8.1,
      pm10: 12.2,
      no2: 10.9,
      ozone: 112,
      airObservedAt: '2026-09-18T15:00',
    },
    fallback: null,
  };
}

test('compass covers the 16 points and wraps', () => {
  assert.equal(compassLabel(0), 'N');
  assert.equal(compassLabel(42), 'NE');
  assert.equal(compassLabel(180), 'S');
  assert.equal(compassLabel(360), 'N');
  assert.equal(compassLabel(NaN), null);
});

test('EAQI bands follow the European scale', () => {
  assert.equal(europeanAqiBand(10), 'Good');
  assert.equal(europeanAqiBand(52), 'Moderate');
  assert.equal(europeanAqiBand(150), 'Extremely poor');
  assert.equal(europeanAqiBand(null), null);
});

test('details normalize a full Milan snapshot', () => {
  const details = atmosphereDetailsFromSnapshot(milanSnapshot(), {
    updatedAt: Date.parse('2026-09-18T17:31:00.000Z'),
  });
  assert.equal(details.hasItem, true);
  assert.equal(details.temperatureLabel, '23.8°C');
  assert.equal(details.conditionLabel, 'Partly cloudy');
  assert.equal(details.windLabel, '4.3 km/h NE');
  assert.equal(details.aqiLabel, '52 · Moderate');
  assert.match(details.pointLabel, /45\.4642.*N/);
  assert.match(details.todayLabel, /17\.4.*26\.1/);
  assert.equal(details.error, null);
});

test('stale snapshots stay usable and say so', () => {
  const snapshot = milanSnapshot();
  snapshot.stale = true;
  const details = atmosphereDetailsFromSnapshot(snapshot, {});
  assert.equal(details.hasItem, true);
  assert.equal(details.stale, true);
  assert.match(atmosphereSectionHtml(details), /CACHED/);
});

test('empty snapshots render the loading state', () => {
  const details = atmosphereDetailsFromSnapshot(null, {});
  assert.equal(details.hasItem, false);
  assert.match(atmosphereSectionHtml(details), /LOADING LATEST READOUT/);
});

test('errors render instead of throwing', () => {
  const details = atmosphereDetailsFromSnapshot(null, {
    error: 'Atmosphere HTTP 502',
  });
  assert.match(atmosphereSectionHtml(details), /Atmosphere HTTP 502/);
});

test('escaping neutralizes markup', () => {
  assert.equal(escapeAtmosphereText('<b>&"'), '&lt;b&gt;&amp;&quot;');
});
