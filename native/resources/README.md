# Bundled reference data

The canonical datasets and transit registry are versioned here and staged into the
application at build time. There is no generated copy and no catalog-generation
dependency.

- Edit `transit-feeds.json` directly when admitting a provider. Retain its license,
  attribution, HTTPS endpoint, center and radius. Do not enable a source without
  compatible terms. The native transit tests validate the registry.
- Update the datacenter, dam and cable snapshots only with licensed source data.
  Retain each resource directory's README, source attribution and extraction
  provenance. The reference-layer tests cover admission and loaded geometry.
- `terrain-attribution.md` records the attribution required by the Mapzen terrain
  data providers. It is bundled with the generated credits.
- `SGP4-LICENSE.txt` covers the Vallado SGP4/SDP4 implementation in `native/sgp4/`;
  see `native/sgp4/UPSTREAM.md` for its upstream revision.
- `native/desktop/Tests/Fixtures/sgp4-reference.json` contains frozen comparison
  vectors that were generated during the native port, plus Vallado verification
  cases. Keep these independent reference values; do not regenerate them using
  Helios's own implementation.

Build and test scripts live one directory above. No Node.js installation is needed
to maintain, test or build the application.
