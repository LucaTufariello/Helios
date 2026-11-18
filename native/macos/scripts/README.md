# Native reference data

The canonical bundled datasets and transit registry are now in
`../Sources/HeliosCore/Resources/`. They are versioned native resources; there is
no second JavaScript copy or catalog-generation dependency.

- Edit `transit-feeds.json` directly when admitting a provider. Retain its license,
  attribution, HTTPS endpoint, center and radius. Do not enable a source without
  compatible terms. The native transit tests validate the registry.
- Update the datacenter, dam and cable snapshots only with licensed source data.
  Retain each resource directory's README, source attribution and extraction
  provenance. `ReferenceLayerTests` cover admission and loaded geometry.
- `../Tests/HeliosCoreTests/Fixtures/sgp4-reference.json` contains frozen comparison
  vectors originally generated with satellite.js 6 during the native port, plus
  Vallado verification cases. Keep these independent reference values; do not
  regenerate them using Helios's implementation under test. Native SGP4 source
  and its upstream revision are documented in `../Sources/CSGP4/UPSTREAM.md`.

Build and test scripts live one directory above. No Node.js installation is
needed to maintain, test, or build the app.
