// Development-only catalog packaging; the delivered macOS app never executes JavaScript.
import { cp, rm, writeFile } from 'node:fs/promises';
import { fileURLToPath } from 'node:url';
import { resolve } from 'node:path';
import { TRANSIT_ENABLED_FEEDS } from '../../../src/data/transitFeeds.js';
const root = fileURLToPath(new URL('../../../', import.meta.url));
const resources = resolve(root, 'native/macos/Sources/GodEyeCore/Resources');
for (const name of ['datacenters', 'dams', 'telegeography_submarine_cables']) {
  await cp(
    resolve(root, 'src/data/local_data', name),
    resolve(resources, name),
    { recursive: true },
  );
}
// Do not let a previous native catalog survive regeneration after its removal.
await rm(resolve(resources, 'neighborhoods'), { recursive: true, force: true });
const operators = TRANSIT_ENABLED_FEEDS.map(
  ({
    id,
    name,
    url,
    license,
    licenseUrl,
    attribution,
    headers = {},
    center,
    loadRadiusKm,
  }) => ({
    id,
    name,
    url,
    attribution,
    license,
    licenseUrl,
    headers,
    lat: center.lat,
    lon: center.lon,
    radius: loadRadiusKm,
  }),
);
await writeFile(
  resolve(resources, 'transit-feeds.json'),
  JSON.stringify(operators, null, 2) + '\n',
);
console.log(
  `Copied the existing licensed reference layers and ${operators.length} enabled transit operators.`,
);
