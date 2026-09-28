# GodEye

A local-first, open-source geospatial workspace for exploring public data, inspecting sources, and working with dated satellite imagery. Runs in your browser without an account or keys to start. Optional providers have their own terms, quotas, and costs.

> Freshness honesty first: the basemap is a baked daytime mosaic (months/years old), not live. Satellite views here are **days old at best, never live video**. Sensor modes (CRT / NVG / FLIR) are shaders, not night imagery.

## What this is

GodEye fuses public feeds into one explorable CesiumJS globe:

- Public position feeds (aircraft, vessels, transit) and explicitly modelled satellite positions
- Daily satellite freshness (NASA GIBS + Sentinel latest-clear) with acquisition dates and cloud %
- Searchable source catalog, selection, tracking, contacts roster, and map marker overlays
- Voice control (optional OpenAI key), share links, global context staging

Standing rules for this fork:

- **FREE DATA ONLY.** No paid Planet / Maxar / Capella / Up42 / SkyWatch tasking, ever.
- No hardcoded keys, no stored third-party tiles, no face / plate detection, no named-person search.
- People are not a query type. Events, assets, infrastructure, and systems only.

## Free satellite freshness

The reason this fork exists — the basemap alone cannot answer "what changed recently".

### Sat Fresh (free) — `sat-fresh`, toggle `k`

- Source: NASA GIBS WMTS (`gibs.earthdata.nasa.gov`, `GoogleMapsCompatible_Level9`, max level 9), date = yesterday UTC.
- Modes: VIIRS TrueColor daily + Terra fallback, Black Marble night lights on the night side, thermal modes. Opacity control, overlay above basemap.
- Honest copy built in: `Basemap = daytime mosaic (months/years old) — not live. Use Sat Fresh for yesterday.` Resolution note: `~250m — neighborhoods, not cars`.
- Credit: `NASA Worldview / GIBS`.

### Solar terminator

- Pure-math subsolar point, sun elevation, day/night state (`src/data/terminator.js`).
- Yellow day/night polyline refreshed every minute. Explains why a baked-daylight basemap looks wrong at night.

### Sentinel Latest-Clear (free) — `sentinel-free`, toggle `y`

- Keyless public search via Copernicus Dataspace **OData** (`catalogue.dataspace.copernicus.eu/odata/v1/Products`). STAC v1 has no raw SENTINEL-1/2 collections, so OData is used deliberately.
- S2: prefers `MSIL2A` (L2A first, L1C fallback), lowest cloud cover wins. S1: most recent (radar — no cloud concept).
- Server routes: `/api/sentinel2-latest?lat=&lon=`, `/api/sentinel1-latest`, `/status`. Budgets: 60/min per IP, 8 MB cap, `gods-eye-view-sentinel-proxy/1.0` UA, 60 min memory + disk cache (`.gev-cache/sentinel-*.json`), serve-stale, 502-with-link-outs accepted.
- Optional free Copernicus Dataspace account (`CDSE_USER` / `CDSE_PASS` in PROVIDER SETTINGS) is reserved for future authenticated download. Search works without it.
- Context-panel section: when enabled, a `SENTINEL LATEST-CLEAR` card renders in the right-side CONTEXT panel (`#context-sentinel-host`, independent of CONTACTS / SPACE MISSIONS tabs): product name, collection, acquisition datetime, cloud % (`N/A — RADAR` for S1), queried point, footprint bbox, fetch time, stale / error / fallback states, plus COPERNICUS BROWSER + EO BROWSER deep links. Appears on enable, refreshes per fetch, removed on disable.
- Credit: `Copernicus Sentinel, ESA/EU (10m S2 optical / S1 SAR preview — days old, not live)`.

## Layers on the globe

Keyless unless noted. Some offer more with a free provider key.

| Layer | What you get | Source | Auth |
| --- | --- | --- | --- |
| Map Stack | Esri satellite, OSM fallback, optional ion-hosted stacks / Google Photorealistic 3D | Esri / OSM / ion / Google | Keyless base; 3D via ion or Google key |
| Sat Fresh | Yesterday's ~250m daily imagery + night lights + terminator | NASA GIBS | Keyless |
| Sentinel Latest-Clear | Latest S2 scene (date + cloud %) or S1 SAR footprint + browser link-outs | Copernicus Dataspace OData | Keyless search; free account reserved for download |
| Atmosphere | Current conditions + today/tomorrow + European AQI for the camera point (model readout, not a station) | Open-Meteo | Keyless (CC BY 4.0, non-commercial) |
| Live Flights | Public aircraft positions + route history | OpenSky + adsb.lol | Keyless (OpenSky key = more credits) |
| Military Flights | ADS-B military traffic | adsb.lol | Keyless |
| Live Vessels | Ships worldwide | AISStream | Free key |
| Satellites | Catalog with class colors + Starlink DENSE option, SGP4 orbits | CelesTrak | Keyless |
| Earthquakes | Last 24h global seismic activity | USGS | Keyless |
| Active Fires | Trailing-24h detections | NASA FIRMS | Free key |
| Space Missions | Rolling 30-day launches: pad, vehicle, payload, recovery | Launch Library 2 | Keyless (token raises allowance) |
| Traffic | Measured aggregate congestion on OSM road segments; no individual vehicle tracking. Keyless = off with key prompt, nothing simulated | TomTom + OSM | Free key required |
| CCTV Mesh | Public camera catalog and available stills/streams; estimated poses can be calibrated | City APIs | Keyless |
| Radio | Geolocated stations with analog tuner | Radio Browser | Keyless |
| Transit | Live buses/trams/metros/trains/ferries with delayed playback | Operator GTFS-Realtime | Keyless |
| Bikeshare | Live station availability (US systems + Milan BikeMi) | GBFS | Keyless |
| Directions | Click A/B street-following route, draped on terrain, flyable | OSRM / OSM | Keyless |
| Mapped Installations | Viewport-bounded community-mapped site context, labeled incomplete | OpenStreetMap | Keyless |
| Energy & Refining | Power plants, substations, refineries from community mapping (capped, incomplete by nature) | OpenStreetMap via Overpass | Keyless |
| Disaster Alerts | Current floods, quakes, cyclones, volcanoes, droughts (severity-coded) | GDACS | Keyless (public domain) |
| Bundled infrastructure | Datacenters, dams, submarine cables | Bundled extracts | Keyless |

Italy coverage, honestly stated:

- **Milan works today:** default start view, Duomo/Castello/Centrale/Bosco Verticale/San Siro camera presets, live BikeMi availability (319 stations, NLOD 2.0), voice fly-to for Milan and Rome presets.
- **Rome transit is researched but OFF:** ATAC/Roma TPL publishes a live keyless GTFS-RT feed (verified ~1,300 Rome vehicle positions), but its terms limit use to trip support and warn against aggregated display, with no explicit redistribution grant — so it is registered in `src/data/transitFeeds.js` with `defaultEnabled: false` (one-line flip once terms allow). See `DATA_SOURCES.md`.
- **No Italy CCTV source yet:** Autostrade per l'Italia (1,000+ webcams) and ANAS traffic cameras are embedded in map apps with no open data endpoint or display license — scraping them is out. The first Italian operator to publish an open camera API gets a pack.
- **No Milan transit realtime yet:** ATM Milano publishes GTFS static only; no keyless openly-licensed vehicle-positions feed found. Same bar as above applies.

Also on the globe: neighborhood overlays, optional cockpit WX cloud effect.

Removed fictional presentation: cinematic Scene Director, Nepal/Bhote Koshi pack, launch ascent replay, guessed orbital tracks and recovery locations, keyless traffic simulation, invented individual traffic vehicles, TR-3B aircraft conversion, classification banners, fake satellite IDs, and recording indicators. Launch records remain; orbital tracks require a matching TLE catalog entry and are labelled as propagation. Missing coordinates remain missing.

The September 2026 review also introduces a quieter workspace layout, searchable data layers, selection filtering, and responsive panels. Read the [project review and staged roadmap](docs/PROJECT-REVIEW-2026-09-28.md) for the changes, verification, remaining limitations, and next steps toward a free public-data analysis workspace.

## Interactions

- **Click-to-track anything:** camera lock, fading trail, metadata card; tracked fire/vessel offers nearest-camera handoff.
- **Cockpit:** ride a tracked flight with terrain holding underneath; sensor styles ride along.
- **Contacts:** 250 km roster near your target, step through and drop into any cockpit.
- **Map markers:** screen-space boxes and record IDs, with density control. These identify loaded map objects; they do not perform computer vision on imagery.
- **Sensor looks:** `1`–`7` (CRT, NVG, FLIR, Noir, Snow, etc.) — visual styles over the normal globe.
- **Telemetry HUD:** camera-derived position, altitude, sun, clock, locality. No classification banners.
- **Global Context:** stage the situational picture with one switch, restore on exit.
- **Share Links:** camera, style, layers, one tracked target serialize into a URL.
- **Reset Globe:** one control back to full Earth.
- **Draw:** DISPLAY ▸ **Draw** turns the globe into a whiteboard drawn by hand — pick Area, Line or Pin, click vertices on the real world, Enter finishes, Esc cancels, Clear wipes the board. Same persistence as voice annotations, no microphone needed.
- **Voice (needs OpenAI key):** realtime agent with ~27 tools — camera verbs, annotations, analyst queries, console operation. Without a key the mic reports unavailable; the rest of the app runs.

## Quick start

Requires **Node.js 24.14+ or 26.x** (Node 25 is end-of-life).

```bash
git clone https://github.com/LucaTufariello/GodEye.git
cd GodEye
npm ci
npm run doctor
npm run dev
```

Open `http://localhost:4173`. Pick **Live Contacts**, **Space Missions**, **Environmental**, or **Explore Manually** from the first-run panel.

First-run default view is Milan, IT (Duomo di Milano 45.4641, 9.1919) — the home turf. Curated camera presets also cover Rome plus Austin, San Francisco, New York, Tokyo, London, Paris, Dubai, and Washington DC, and voice "fly to the Duomo" / "fly to the Colosseo" resolves through the same preset table.

Keyboard: `1`–`7` styles · `H` HUD · `D` detection · `C` cockpit · `K` Sat Fresh · `Y` Sentinel · `Esc` out.

macOS shortcut: `./scripts/dev-fresh.sh` clears the Vite cache and pulls configured keys from the Keychain. Starts keyless too.

## Keys (upgrades, not prerequisites)

Add keys in-app via the **PROVIDER SETTINGS** chip (Provider Settings), not in files. Terminal clones read repo-root `.env` (see `.env.example`); server binds localhost; Provider Settings answers only from your machine.

| Key | Why | Where |
| --- | --- | --- |
| Cesium ion | Google Photorealistic 3D + world terrain (eligible personal/non-commercial use; quotas apply) | cesium.com/ion |
| Google Maps | Direct 3D + place search (metered) | Google Cloud Console |
| OpenAI | Voice + AI HUD summary (metered; session cap enforced in-app) | platform.openai.com |
| AISStream | Live ships | aisstream.io (free) |
| NASA FIRMS | Live fires | firms.modaps.eosdis.nasa.gov (free) |
| Copernicus Dataspace | Sentinel authenticated download (search already keyless) | dataspace.copernicus.eu (free) |
| TomTom | Traffic flow (required for that layer) | developer.tomtom.com (free tier) |
| OpenSky / LL2 | Higher polling allowances (optional) | opensky-network.org / thespacedevs.com |

`npm run doctor` reports readiness and where each provider was found without printing values. Restrict browser keys at their providers. See `SECURITY.md` before sharing on LAN (`--host 0.0.0.0` brokers your keys to anyone who can reach it; set throttles + provider budgets).

## Verify

```bash
npm test                  # unit suite
npm run check:boundaries  # package export ownership
npm run format:check      # prettier scope (~870 files)
npm run doctor            # env + provider routes
npm run build             # production build
PORT=4173 npm run dev -- --port 4173 --strictPort
curl "http://localhost:4173/api/sentinel2-latest?lat=45.4642&lon=9.19"
```

Docs: `docs/CURRENT-STATE.md` (runtime reference), `docs/APPLICATION.md`, `docs/CODE-BOUNDARIES.md`, `DATA_SOURCES.md` (per-source terms), `SECURITY.md`, `TESTING.md`, `GEV_FREE_SATELLITE_PLAN.md` (free-satellite handoff + next tasks).

## Limits (read before operating)

Exploratory visualization of public/third-party data. Data may be delayed, incomplete, modeled, inferred, or wrong. Do not use for flight or maritime navigation, emergency response, medical/health, investment, or other safety-critical or operational purposes. Verify with authoritative sources.

- Basemap is old by design; use Sat Fresh (yesterday) and Sentinel (dated scenes) for recency, and read the dates.
- Terrestrial AIS goes quiet mid-ocean; satellite AIS costs real money.
- Premium imagery, SAR tasking, and deep commercial feeds live behind enterprise contracts — every layer here is a pattern to point at your own sources.

## Responsible use

Public data, clear sources, local-first execution. This project models events, assets, infrastructure, and systems — not named-person search, face recognition, or individual tracking. Contributions crossing that line are out of scope.

## License

Source code under MIT — see `LICENSE` (copyright notice preserved there as required). Bundled datasets (`src/data/local_data/`), 3D models (`public/models/`), and all runtime-fetched provider data carry their own licenses/terms — see `DATA_SOURCES.md` and per-folder READMEs. Remove any dataset whose terms don't fit your use.
