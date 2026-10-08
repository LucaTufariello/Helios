// Developer-only comparison vectors from the existing application's orbital engine.
import { readFile, writeFile } from 'node:fs/promises';
import { twoline2satrec, sgp4 } from 'satellite.js';
const file = new URL(
  '../Tests/GodEyeCoreTests/Fixtures/sgp4-reference.json',
  import.meta.url,
);
const previous = JSON.parse(await readFile(file, 'utf8'));
const catalogs = [...new Set(previous.map(({ tle }) => tle))];
// Vallado verification case 24208, ITALSAT 2: low-inclination deep-space propagation.
const geostationary = `ITALSAT 2
1 24208U 96044A   06177.04061740 -.00000094  00000-0  10000-3 0  1600
2 24208   3.8536  80.0121 0026640 311.0977  48.3000  1.00778054 36119`;
if (!catalogs.includes(geostationary)) catalogs.push(geostationary);
const fixtures = catalogs.flatMap((tle) =>
  [0, 60, 360, 1440].map((minutes) => {
    const [, line1, line2] = tle.split('\n');
    const result = sgp4(twoline2satrec(line1, line2), minutes);
    if (
      !result?.position ||
      !Object.values(result.position).every(Number.isFinite)
    )
      throw new Error('Legacy engine rejected the verification case');
    return {
      tle,
      minutes,
      position: [result.position.x, result.position.y, result.position.z],
    };
  }),
);
await writeFile(file, JSON.stringify(fixtures, null, 2) + '\n');
console.log(
  `Updated ${fixtures.length} reference vectors using the existing satellite.js dependency.`,
);
