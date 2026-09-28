# Project review — 28 September 2026

GodEye has a useful foundation for an open-source public-data analysis workspace: Cesium map and terrain integration, public feed adapters, modular source/render/lifecycle owners, local provider settings, bounded proxy requests, attribution, selection, annotations, and share restoration. Its main weakness was presentation that blurred the boundary between observations, estimates, and fictional effects. The current application remains a viewer and workspace foundation; it does not yet provide the full investigation, evidence, and collaboration workflows implied by the long-term Maven-inspired goal.

This review builds on the changes already present in the working tree. Existing Atmosphere, Energy & Refining, Disaster Alerts, Milan coverage, and earlier removal of cinematic/classification features are retained. Upstream attribution and the MIT license remain in place.

## Changes made

### Source integrity

- Removed the TR-3B conversion registry, sprites, UI button, and aircraft query/tracking overrides. Real identifiers, aircraft classes, and operator metadata survive the normal civil/military pipeline.
- Replaced animated traffic dots with measured aggregate congestion on matched road segments. There are no individual traffic vehicles, invented speeds, density simulations, unknown-road substitutes, or traffic detections. Keyless use reports KEY REQUIRED and requests no road/flow data. A failed flow request clears its overlay.
- Removed guessed orbital rings and payload positions from launch metadata. Launch manifests and reported pad locations remain available without an orbit. Orbital tracks require a matching satellite catalog record; labels say TLE propagation. This is a model derived from orbital elements, not a live position observation.
- Removed inferred recovery coordinates. A launch-pad proxy or guessed downrange endpoint is no longer presented as a recovery site.
- Reject missing, blank, boolean, and invalid launch/disaster coordinates rather than coercing them to 0,0. A valid explicitly reported zero coordinate remains valid. GDACS parsing handles GUID attributes and XML entities; its cache schema changes so older malformed cached positions are not reused.
- Removed the hardcoded placeholder HUD date. Loading copy now says source data, because some sources are reference datasets or model products.
- Kept visual filters, tracking models, dead reckoning, camera calibration, and weather effects where they support useful navigation. Copy distinguishes presentation effects from sensor evidence. Map marker boxes identify loaded records; they do not claim computer vision on satellite imagery.

### Efficiency and lifecycle behavior

- Road-condition geometry is capped at 1,200 matched segments. Supported scenes use at most four ground-polyline batches; a simpler static polyline collection is the fallback.
- Traffic no longer registers a frame animation or holds continuous rendering. It requests a redraw when source geometry changes. Cached road geometry supports a 120-second parked-view flow refresh.
- Cancellation, load generations, per-request identity, and road-set identity prevent old or partial flow work from replacing a newer full result. Paint-race timers are cleared after settlement; disable/destroy clears requests, retry timers, geometry, and camera subscriptions.
- Flow freshness is updated only by successful flow responses, not by geometry redraws. Coverage reflects matching across the loaded road set.
- The source catalog caches DOM handles and searchable metadata. It updates changed text and visibility, and retains legend button identity when content has not changed. This reduces repeated queries and DOM rebuilding while preserving keyboard focus.
- Removed dead traffic simulation/queue/preset owners and retired simulation QA scripts. The traffic QA entry now runs source, renderer, and navigation regression tests.

These are structural reductions in work. No new hardware FPS or battery benchmark was collected, so this review makes no quantified frame-rate claim. The main bundle still triggers Vite's chunk warning; lazy loading is a future measured change.

### Workspace design

- Quiet slate panels and a stable header replace the strong surveillance styling.
- The left catalog opens by default on desktop, with search by layer/provider, Selected only, source state, counts, and an empty-result message.
- The right context panel explains how to inspect contacts and launch records. Display and camera panels remain accessible but start compact.
- Navigation, display controls, and optional voice share a smaller bottom dock. Voice keeps its model/cost indication and is described as an optional paid service.
- Basemap freshness remains visible in the footer. Provider credits and Data attribution remain on screen.
- Provider settings and first-run copy explain what keys unlock, and distinguish paid services from public feeds.
- Narrow screens open one major panel at a time so the catalog and context controls do not overlap. Mobile controls and credits retain distinct space.

## Verification

The full unit suite includes source, lifecycle, tracking, rendering, catalog filtering, and integrity regressions. The separate GDACS provider suite passes all three tests. Production build, import/export ownership checks, formatting of 890 adopted files, and browser smoke checks pass. The final run on Node 26.8.1 reports 3,900 cases: **3,899 passed, zero failed, and one intentionally skipped**.

The production main JavaScript bundle is 1,890.44 kB (560.29 kB gzip), compared with the initial working-tree build at 1,909.56 kB (566.17 kB gzip), about 1% smaller. Main CSS grows from 195.95 kB to 208.27 kB as the workspace layout is introduced. This is a build comparison, not a startup/FPS benchmark.

Browser review covers the desktop workspace and a 390 × 844 viewport, source search, selection filtering, a real USGS earthquake response, display controls, first-run copy, and provider settings. Existing keyed services are not evidence that every keyless deployment has the same 3D imagery. TomTom rendering is verified with deterministic flow fixtures; no live keyed TomTom end-to-end result is claimed.

Two allocation-budget test files are excluded by the repository runner on Node 26 because their calibration targets Node 24. This review does not treat those omitted checks as passed. The repository's intentional skipped case remains skipped.

## What “free” means today

| Capability                                                     | Current dependency                           | Practical limit                                                                  |
| -------------------------------------------------------------- | -------------------------------------------- | -------------------------------------------------------------------------------- |
| Basic globe, public positions, earthquakes, reference layers   | Public/keyless services and bundled extracts | Feed outages, regional coverage, source age, and provider terms still apply      |
| Dated NASA imagery and Sentinel catalog inspection             | Public imagery/catalog endpoints             | Resolution, clouds, latency, and product availability; no live satellite video   |
| Vessels, FIRMS fires, measured road conditions                 | User-supplied provider credentials           | Free access/allowances are source-specific; quotas and terms need ongoing review |
| Photorealistic 3D, enhanced place search                       | Optional ion/Google services                 | Eligibility, quotas, and possible charges; basic map use does not require them   |
| Voice and AI-assisted actions                                  | Optional OpenAI service                      | Paid usage; not required for basic exploration, drawing, or map inspection       |
| Cases, linked evidence tables, unified timeline, collaboration | Not yet implemented                          | This is the next product work, not an existing feature                           |

Open-source code does not make every upstream dataset freely redistributable or every provider request free. `DATA_SOURCES.md` is the per-source inventory; deployments need to respect current terms. The free baseline should remain useful without Google or OpenAI credentials.

## Recommended development sequence

1. **Evidence and local cases.** Introduce a common record inspector with source URL, source observation time, fetch time, spatial coverage, license, and whether a field is reported, estimated, or propagated. Save areas of interest, notes, selected records, and source references locally. Export a case as portable JSON/GeoJSON plus human-readable evidence. Keep local editing usable without AI.
2. **Linked table and map.** Add validated GeoJSON/CSV import, a searchable result table linked to map selection, reusable filters, and safe bounded result sets. Missing values should remain explicit. Test import schemas and round-trip exports before adding new feeds.
3. **Time and comparison.** Separate observation time from playback time and polling time. Add consistent time filters and dated imagery comparison. Change detection should expose the method and input dates and never invent a result when inputs are unavailable.
4. **Optional self-hosted collaboration.** Add case ownership, access controls, change history, backups, and export before shared editing. Keep a fully functional single-user path and avoid making any paid provider a prerequisite.
5. **Measured loading and rendering work.** Profile cold startup and idle/active scenes with representative data, then split expensive optional owners along the existing boundaries. Consolidate workspace and legacy CSS with visual regression coverage for credits, overlays, panel collapse, and keyboard use.

Success should be measured by how easily someone can answer “what is this, where did it come from, when was it observed, how certain is it, and can I reproduce/export my work?” Adding more moving symbols alone will not achieve that goal.
