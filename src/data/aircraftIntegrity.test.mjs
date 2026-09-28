import test from 'node:test';
import assert from 'node:assert/strict';
import flights from './flights.js';
import military from './militaryFlights.js';

for (const layer of [flights, military]) {
  test(`${layer.id}: analyst records preserve source aircraft class and missing identity`, () => {
    const record = layer.mapAnalystRecord('abc123', {
      klass: 'helicopter',
      callsign: ' ACTUAL1 ',
      rawLat: 45,
      rawLon: 9,
    });
    assert.equal(record.aircraftClass, 'helicopter');
    assert.equal(record.callsign, 'ACTUAL1');
    assert.equal(record.lat, 45);
    assert.equal(layer.mapAnalystRecord('abc123', {}).aircraftClass, null);
    assert.equal(layer.mapAnalystRecord('abc123', {}).callsign, null);
  });
}
