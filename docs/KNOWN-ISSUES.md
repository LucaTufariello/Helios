# Known limitations

The maintained capability/limitation summary is [CURRENT-STATE.md](CURRENT-STATE.md).
Workflow-specific details are in the satellite, Mission Control and native rendering
guides. Report reproducible native bugs with macOS/architecture, commit, steps,
selected source/product and sanitized diagnostics.

- Slow or interrupted optical archive reads may take several minutes. Bounded
  retries and local diagnostics are available; cancellation cleans partial outputs.
- SAR requires a valid arm64 raster environment and compatible SNAP GPT. Orbit/DEM
  availability and provider changes can fail real processing despite valid input.
- Numerical tests do not establish absolute radiometric accuracy or independent
  geographic alignment. Those validation tasks remain on the satellite roadmap.
- Some provider formats, camera integrations and 3D material/extensions are unsupported.
  Missing coverage and decoder failures remain explicit.
- The app is locally signed, not notarized. A separate macOS 14 runtime acceptance
  check and full manual pointer-interaction coverage remain outstanding.

See [TESTING.md](../TESTING.md) for reproducible checks and [SECURITY.md](../SECURITY.md)
for private vulnerability reporting.
