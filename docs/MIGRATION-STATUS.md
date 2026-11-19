# Native application status

Updated 19 November 2026. Helios is a C++20/Qt 6 desktop application for macOS and
Linux. The earlier Swift macOS client was removed; the Qt client is the only
application. Its enabled workflows are implemented in Qt, while platform and
scientific acceptance remain **partial**.

The source layout is now C++ and Python only:

- `native/desktop/` — Qt client and tests.
- `native/scene/` — Cesium Native scene adapter.
- `native/shared/` — portable WGS84/geometry library.
- `native/sgp4/` — Vallado SGP4/SDP4.
- `native/workers/` — scientific Python workers, their tests and validation tools.
- `native/resources/` — canonical bundled datasets and transit registry.

## Linux evidence (historical baseline)

Source revision: `2660808b2c3cb18a756d2b6a91f51b2558a6f9f5` (also retained in
`revision.txt`); initial working tree clean. Later migration work is recorded below.
Host: Omarchy 4.0.4, x86_64, kernel 7.2.5-3-omarchy,
Hyprland/Wayland, NVIDIA GeForce GTX 1070 (8 GiB), driver 580.178.04, 15 GiB
reported system RAM. Vulkan reports the NVIDIA discrete device. No GPU driver,
CUDA, desktop configuration or system package changes were made.

Build: GCC 16.2.1, Qt 6.11.2, CMake 4.4.4. Qt Base, Shader Tools and Wayland were
already installed. CMake was missing, so a local Python environment under ignored
`output/tools/` supplied it; this is a development prerequisite, not an application
runtime dependency. The current binary is `output/desktop/build/bin/Helios`.

Evidence root: `output/qa/linux-20261015T100735Z/` (ignored, local to that machine).

| Check | Result | Evidence |
| --- | --- | --- |
| Read-only host/GPU diagnostics | Vulkan exposes the GTX 1070 in the real Wayland session | `environment.txt` |
| `make test-shared` with local CMake on PATH | Exit 0; 1,885 spatial checks | `shared.log` |
| `make test-desktop` with local CMake on PATH | Exit 0; both CTest suites pass | `desktop.log` |
| Qt core detail | 13 passed, 0 failed, 1 skipped: opt-in real mission library unavailable | `core-details.log` |

## Scene and process migration increment

The Qt client uses the shared C++ scene preparation, materials and picking code in
`native/scene/`, with the unchanged pinned Cesium revision. Qt has bounded,
cancellable transport and a floating **3D Surface** panel for local/HTTPS loading,
focus, removal, credits and errors. Scene textures are shared, with a separate
256 MiB visible GPU budget.

Evidence: `output/qa/linux-migration-20261015/`, including source SHA-256 hashes.

| Check | Result |
| --- | --- |
| Desktop build / CTest at this increment | Eight suites passed: spatial, desktop core, scene, worker, optical catalogue, credentials, radar catalogue/account and downloads |
| Generated Vulkan integration | 44 checks; roof picking, glTF colour, idle frames and repeated cancellation/removal; inspected `generated-scene/generated-scene.png` |
| Real public scene integration | 19 checks; AGI headquarters photogrammetry plus public basemap/terrain; inspected `live-scene/existing-scene.png`; one mesh, 100% load progress, zero load/format failures |
| Short generated scenario | 3.574 s elapsed, 0.617 process CPU s, 272.2 MiB peak resident memory |
| Short live scenario | 7.632 s elapsed, 6.355 process CPU s, 331.3 MiB peak resident memory |
| Process boundary | Real event/argument, inherited lock, child cancellation, unrelated-process preservation, incomplete cleanup and recovery tests pass |
| Raster workers | 62 tests pass in standalone mode; 62 pass in the new owned-session mode; GPT execution remains mocked |
| Optical catalogue | Sentinel-2 UTC/cloud/asset admission and CBERS deterministic archive/unknown-cloud tests pass |

The worker boundary uses atomic records and bounded logs/events. Recovery uses a
live inherited lock, ignores saved PIDs and preserves old data. Cancellation
targets the session led by the active QProcess; only known partial files in new
owned jobs are cleaned, while completed atomic results are retained. Worker
resources are copied beside the executable/bundle and installed by CMake.

The processing window provides native optical catalogue/search/retrieval, local
PRISMA modes, GRD inspection/processing controls, engine preflight, saved jobs and
recovery. Sentinel-1 controls include UTC/mode/polarization search, bounded
pagination, actual footprint display, session-only sign-in/refresh, source-based
archive download, byte progress and cancellation. Download tests cover no
replacement, provenance, unsafe redirects, received/announced byte limits, length
mismatch, ZIP envelope admission and partial cleanup. Full SAFE, ZIP/CRC/XML
admission remains in the scientific worker.

Further evidence in the same root:

- `generated-processing-host/`: 52 Vulkan checks passed through the owned process
  host, generated PRISMA HDF processing, library/map activation and scientific
  exports. This is generated data, not ASI scientific acceptance.
- `public-optical/`: 27 checks passed; real search → retrieve → view → link →
  save/reopen → original export. Source `S2B_T32TNR_20261012T102038_L2A`, acquisition
  `2026-10-12T10:28:13.131000Z`, 40 × 56 preview, bounds 9.18,45.455,9.185,45.46.
  Elapsed 10.7 s, parent CPU 9.944 s, parent peak RSS 342.6 MiB.
- `secret-store.log`: four actual save/read/delete checks passed with a generated
  temporary key in this PC's unlocked Linux Secret Service. Optional library
  loading allows startup without a secret service; there is no plaintext fallback.
  Fresh macOS Keychain execution remains unverified.
- `public-radar/`: 21 checks passed, 20 actual Copernicus acquisitions and source
  footprints returned; no authenticated archive was downloaded. The corrected run
  in `public-radar-corrected/` passes 21 checks and retains visually inspected map
  and footprint frames.
- `desktop-eight-suites.log`: all eight CTest suites pass. Credential OS-store and
  copied real mission checks remain opt-in in the default suite.

## Mission, domain, inference and packaging increments

Evidence is retained under `output/qa/linux-migration-20261015/` and the latest
`output/qa/linux-migration-20261016/`. These are separate runs against their
recorded source state; their counts overlap and must not be added into a single
test total. An additional native validation exposed synchronous GPU-status
callbacks reentering rendering during a layout resize. Coalesced queued
notifications corrected it; the fresh native/resource runs below supersede that
failure. `render-crash-correction.json` retains the core diagnosis, exact source
hashes and rerun evidence.

| Check | Observed result / evidence |
| --- | --- |
| Integrated native build and regression suites | `render-fixed-tests.log`: all 16 CTest suites passed in 17.24 s, including Domain UI, MapTools and configured CPU inference. `raster-tests.log` passed all 63 shared scientific fixture tests |
| Storage, infrastructure and mission operations | Ten CTest suites passed in `desktop-ten-library-suites.log`, including owned-job Trash admission, backups/conflicts, timeline filters/CSV escaping, comparisons, monitor baselines/deduplication/concurrency and persistence |
| Generated infrastructure workflow | `review-current/` and `mission-tools/` each passed 63 Vulkan checks, including generated calibrated-power change review through the actual shared worker, site/source UUIDs, review draft persistence and map preview; `raster-infrastructure.log` passed 63 fixture tests |
| Actual CBERS-4A native acquisition | `cbers-live/cbers-live-report.json`: 34 headless checks passed through the native catalogue and WorkerHost for `CBERS_4A_WPM_20261003_198_130_L4`, acquired 3 October 2026 12:43:23 UTC. PAN was 55 × 56 at 2 m; native RGB 14 × 14 at 8 m. The tiny window has constant PAN/red/blue DN channels and is unsuitable for feature/image-quality claims; transfer, grids, DN exports and provenance are verified, not independent sensor accuracy |
| Merged native workflow acceptance | `provider-correction-native/validation.json`: 104 GTX 1070 Vulkan checks passed in 12.325 s (parent CPU 7.524 s, peak parent RSS 337,674,240 bytes). Coverage includes timeline comparison, exact UTC task deadline, annotation/workspace persistence, frozen evidence geometry across refresh, monitor checkpoint/reopen, processing/review, CPU proposal source verification/analyst review and safe map caption/layout changes |
| Current public acquisition modes | `provider-optical/validation.json`: 25 checks passed through native search, owned process host, library/map, mission reopen and original exports for the identified 40 × 56 Sentinel-2 preview. `provider-radar/validation.json` retains 20 actual acquisitions/footprints with no archive download |
| Daily NASA visualization sources | `daily-imagery/daily-live-tests.log`: 12 checks passed without skips, retaining actual VIIRS/Terra true-colour, historical night-light and thermal responses. `live-daily/validation.json` passed 19 actual-GPU live checks, including daily imagery over terrain and native display compositing |
| Local assistance, generated execution | `local-inference/`: 15 Python tests, four Qt core checks and three native UI checks passed. Generated input-dependent ONNX execution checks preprocessing, boxes, thresholds, hashes, proposal admission and analyst selection |
| Domain module and native media | `domain-core/results.json`: 22 domain checks passed; `DomainUITests`: five passed, one opt-in skip. `domain-media-ffmpeg/camera-media.json` retains a real TfL 352 × 288 decoded still/clip frame through the owned FFmpeg process; three opt-in checks passed |
| Real keyless domain sources | `domain-core/live-final.json`: 13 of 15 sources succeeded. The 16 October bounded Overpass retry returned eight installation and five energy records with zero rejected in the fixed Milan view; IDs, notices, unknown observation times and retrieval UTC are retained in `domain-live/overpass.json` |
| Map tools and share/annotation/workspace core | Nine core checks pass; the merged native run verifies drawing, annotation reload, private share links, display state and frozen map geometry. The 16 October opt-in retry passed actual Photon and OSRM checks: Photon returned six matches for “Milano Italia” with Milan at 45.4641943,9.1896346; the walking route was 1,036.6 m, 829.4 s, 70 points and 10 instructions |
| Trained local CPU model | `local-inference/trained-product-current/result.json`: pinned Tiny YOLOv2 VOC executed on the actual 40 × 56 Sentinel-2 preview, ONNX Runtime 1.23.2 CPU only, zero proposals at score threshold 0.5, 0.201 s, 454,590,464 peak worker RSS bytes. This verifies execution, not aerial detector accuracy |
| Final Linux package acceptance | `package-release-r3/package-check.json`: all 16 runtime/resource/checksum/install/remove entries pass outside the checkout, including 104 generated native and 19 live Vulkan checks. `package-optical-r3/` adds 25 actual Sentinel-2 checks; `package-media-r3/` passes three actual Wayland/TfL FFmpeg still/clip/pause/seek/cleanup checks with an inspected 352 × 288 frame |
| Owned C++ sanitizers | The post-review rerun under `sanitizers/` passed 14 suites and exposed a UI test assertion racing a queued preview callback; the test now waits for readiness and its rebuilt suite passed in 3.05 s. All 14 separately instrumented scene adapter checks passed. ASAN/UBSAN stop on errors; leak detection disabled |
| Repeated actual-GPU resource scenario | `render-fixed-resources/map-resource-checks.json`: 76 checks passed and three native-minimize checks explicitly skipped because Hyprland did not acknowledge minimization. Three travel cycles used a generated GLB plus two 2048 × 2048 overlays. Peak RSS was 284,569,600 bytes; growth after the first cycle was 442,368 bytes. Settled one-second idle intervals rendered zero frames |
| CMake application-only install | `prefix-install.json`: all 25 files stayed under the requested ignored output prefix, including app, process host, shared SAR/Optical/Inference workers and bundled source resources. Generated Python bytecode was absent |

The accepted local Linux artifact is `output/packages/linux-release-r3-20261016/`,
version `0.1.0.r2660808b.9dbbf39a`. It includes exact Qt 6.11.2 libraries/plugins,
the worker host, portable worker/reference resources, source snapshots and notices.
It also carries FFmpeg 9.0.1 with libav/libsw libraries and a matching
upstream-source/Arch-recipe/patch archive. The portable archive is 35,276,885 bytes
with SHA-256 `a9d206a329e936fa98ff5927e40eaeaabb574291a1c6f5429829b95fce1a35d6`;
the native package is 35,279,200 bytes with SHA-256
`36f092c57915de622c9c00e8f08995b00a36b5323f489d2eaae28ec9b4017e92`.
The final source identity is
`9dbbf39ab7865ad054cf7b20a8d7ccf88bbc5a361914851397232cb6091f774c`.
The package snapshot precedes these result-document updates; its manifest fixes
the exact shipped source/runtime state. Local packaging acceptance does not
establish other distributions, hosted CI, public release licensing or Mac acceptance.

The model is an ordinary VOC everyday-image detector, not a validated aerial,
building or damage detector. No model weights are bundled. CPU is the only offered
backend. NVIDIA/AMD acceleration and the Mac process backend remain unverified.

## Workflow inventory

**Missing** means no Qt workflow; retaining JSON fields is not implementation.
**Partial** means useful code exists but parity or acceptance is incomplete.
Verification is limited to the explicitly named evidence. The latest 16-suite
integration and 104-check native run cover the merged Linux workflows; external
platform and scientific gates remain separate.

Mac evidence below is historical. No fresh Mac build, Keychain execution or Metal
check has run in the Linux session that produced this evidence. Linux and arm64 Qt
build/package CI jobs are configured; no hosted execution is claimed.

| Workflow | Status | Code / tests | Linux evidence | Mac evidence | Remaining work |
| --- | --- | --- | --- | --- | --- |
| Shared WGS84/ECEF, camera bases and ellipsoid/triangle picking | verified | `native/shared`, `Core/Geography`; spatial suite | 1,885 checks; owned-code sanitizers | Historical shared checks | Retain regression coverage |
| GPU globe, imagery, terrain, depth/masks and loaded-surface picking | partial | `Graphics/MapWidget`, `TileCache`; core/native harness | Actual Vulkan GTX 1070; generated/live frames and corrected regional/radar rendering; 76 resource/travel checks passed | Historical Metal | Large photogrammetry/data stress and fresh Mac |
| Camera navigation, coordinate search, keyboard, resize and window lifecycle | partial | Main map/navigation; harness | Tilt/picking/keyboard/resize, three travel cycles, hide/show and scene removal pass | Historical harness | Physical gestures, DPI/multiple displays, actual compositor minimize/restore and accessibility |
| Photorealistic 3D Tiles load/remove/focus, supported triangle content and picking | partial | Shared `native/scene`, Qt transport and 3D Surface; scene tests | Generated/actual AGI geometry and textures, three-cycle scene/overlay resource checks; 14 scene sanitizer checks | Not rerun after source move | Large-site resource/input scenarios and current Mac regressions |
| Map-first Map/Satellite/Marine/Sky/Ground sections and floating tools | partial | `MainWindow`, `DomainWindow`, processing/review/assistance windows | Merged native processing/review/mission/map tools and separate Domain UI checks; compact satellite inspector passes | Historical | Broader manual appearance/focus/input and current Mac |
| Daily true colour, Terra, night lights and thermal visualization | partial | `Core/DailyImagery`, map compositing and source/date controls | Twelve live/cache checks; 19 actual-GPU live checks include daily-over-terrain/display compositing; empty thermal sample disclosed | Historical | Current Mac and broader date/source scenarios; visualization date is not per-pixel acquisition time |
| Portable mission documents, unknown fields, Foundation dates, atomic/conflict saves and backups | partial | `Core/Missions`, `JsonFile`; round trips/conflicts/backup tests | Ten-suite run, generated mission save/reopen and sanitizer checks | Historical fixture compatibility | Extended migrated field compatibility and current Mac |
| Mission create/select/delete, brief, area edit/focus/remove and saved views | partial | `MissionWindow`, `MissionEditor`, `Missions` | Core edit/delete/source-preservation tests; earlier map/view checks | Historical | New UI save/reopen, draft switching/close and linked-deletion acceptance |
| Evidence links, source snapshots/inspection, imagery selection and unlinking | partial | Mission/map/domain integration; bounded snapshot admission | Generated imagery links, annotation/source snapshots and frozen map geometry survive refresh; source-preservation tests | Historical | Extended missing-source/provider scenarios and current Mac |
| Notes/citations, task editing/completion/due dates and drafts | partial | Native entry editors with optional UTC dates and evidence/area links | Core round trips/edit/backup tests; native exact-UTC task deadline and review close/reopen checks | Historical | Broader citation/draft-switch/manual UI and current Mac checks |
| Manual observations, model proposals and separate review decisions/rationale | partial | Observation editor, inference proposal admission and mission review | Native input-dependent CPU proposal saves as Unreviewed, analyst review survives reopen, changed source bytes prevent acceptance | Historical | Current Mac and independent model accuracy/registration checks |
| Timeline, UTC/unknown dates, area gaps, comparisons, snapshots and CSV/JSON exports | partial | `Core/Timeline`, `UI/TimelinePanel`; async bounded previews | Core CSV/date/filter/compatibility/gap checks; native two-preview comparison and mission reopen pass | Historical | Broader manual export/filter checks and current Mac |
| In-app acquisition/evidence monitors, baselines, deduplication, pause/error state | partial | `Core/Monitors`, `MonitorPanel`; two active checks, bounded IDs and due scheduling | Core baseline/pause/concurrency/stale-geometry checks; native local checkpoint/relaunch persistence passes | Historical | Live catalogue monitor and physical sleep/relaunch; no service or automatic product download claim |
| Completed optical/SAR libraries, bounded previews, A/B overlays and original exports | partial | `Products`, `SatelliteWindow`; path/CRS/scientific dimension/export tests | Packaged real Sentinel-2 retrieval/export, generated overlay/blending and compact inspector checks; source data preserved | Historical real optical/SAR | Real GRD VV/VH/composite/nodata, broader large-library/reopen and current Mac |
| Site library, compatible pairs, measured SAR change review, decisions and exports | partial | `Core/Infrastructure`, `ReviewWindow`, shared `change_review.py` | Actual worker on generated calibrated-power grids, decisions/drafts/map; 63 raster fixtures | Historical | Real compatible SAR pair and scientific-quality review; retain one-million-source-pixel cap |
| Jobs: preflight, real events, logs, process cancellation, cleanup and restart recovery | partial | `WorkerJob`, dedicated `WorkerHost`, processing controls | Inherited-lock recovery, cooperative/stubborn descendants, unrelated-process preservation; actual generated/public worker runs | New Mac host untested | Final inference cancellation regression, actual GPT descendants and fresh Mac |
| Sentinel-1 UTC/region/mode/polarization search, footprint, sign-in/download and admission | partial | `RadarCatalog`, `RadarDownload`, `RadarPanel`; account/download negative tests | Corrected public catalogue run: 21 checks, 20 actual acquisitions/footprints; fake authenticated download/cancel/limits | Historical | Actual account/archive retrieval and SAFE inspection; full worker CRC/XML/ZIP rules retained |
| SNAP GRD orbit/noise/calibration/Refined Lee/DEM pipeline and scientific float/dB exports | partial | Shared `sentinel1.py`, native processing/job controls | Fixture/mock GPT tests pass; no actual SNAP | Historical | Identified real GRD + external GPT/DEM/calibration run |
| Sentinel-2 date/cloud search and bounded true-colour retrieval | partial | `OpticalCatalog`, shared worker and native processing controls | Fresh 25-check search → owned process host → retrieve → view → link → reopen → original export; identified acquisition above | Historical | Fresh Mac; broader network interruption/storage/cancellation cases |
| CBERS-4A PAN/native RGB | partial | Query/admission/native controls and shared worker | 34 actual native catalogue/WorkerHost checks; tiny PAN 55 × 56 / 2 m and RGB 14 × 14 / 8 m, preserved identity/time/DN/provenance | Historical | Broader coverage/quality and current Mac; mostly constant selected DN channels limit interpretation |
| Local PRISMA L2D PAN/RGB/spectral import | partial | Native controls and shared `optical_sources.py` | Generated HE5 PAN → library/map/scientific export | Historical | Actual ASI modes/product; ordering remains excluded |
| Worker/resource discovery, settings, missing files and safe library deletion/recovery | partial | Installed workers/source references, `LibraryStorage`, owned lock/hash checks and OS Trash only | Trash refusal/identity/lease/hash tests, missing-source/import checks; final outside-tree package executes shared workers and reference resources | New Mac deployment not rerun | Actual Trash/reimport/relaunch UI and current Mac deployment |
| Record model, selection/inspector/raw data, layer controls and refresh/cancel | partial | `DomainFeeds/Parsers/Network/History`, native domain window and shared-map markers | 22 module checks, six native UI checks; keyless source transport plus recovered Overpass; merged frozen snapshot/live selection map checks | Historical | Broader actual source/history/follow and current Mac |
| Marine: AIS vessels and submarine cables | partial | AIS WebSocket source, bundled cable geometries, optional key setup | Parser/reference module checks; real keyed AIS not run | Historical | Live AIS key/account check, source/latency attribution and native map geometry |
| Sky: live/military flights | partial | OpenSky/regional adsb.lol fallback parsers/network | Actual OpenSky 13,699 / military adsb.lol 250 records; parser and shared native selection/source checks | Historical | Broader regional/fallback/history scenarios and current Mac |
| Sky: satellite groups, SGP4/SDP4, orbit paths and space missions | partial | `DomainOrbit`, native SGP4; source groups/follow/history | 16 Vallado reference vectors/module checks; actual 17 CelesTrak records | Historical | Live group/follow/path/native map check; estimates remain labeled |
| Atmosphere, earthquakes, disasters and FIRMS thermal detections | partial | Provider network/parsers and optional FIRMS key | Actual USGS/Open-Meteo/GDACS source records; no keyed FIRMS acceptance | Historical | Final UI/source timestamps, unavailable-key states and live FIRMS |
| Ground: installations, energy/refining, datacenters and dams | partial | Bounded OSM/Overpass and installed reference datasets/notices | Reference counts/admission pass; 16 October Overpass returns eight installation and five energy records with retained source notices | Historical | Broader regional/UI and current Mac checks; incomplete maps remain explicit |
| Ground: operator GTFS-realtime transit, cameras/stills/clips and road traffic | partial | Native protobuf parser/operator registry, bounded media reader, FFmpeg frame process and TomTom source | Owned FFmpeg/Widgets live media check passes three checks with a real 352 × 288 TfL frame; actual GTFS 521 vehicles and 788 camera records | Historical | Packaged FFmpeg live clip passed three checks; broader operator/provider coverage and keyed TomTom |
| Sentinel product catalogue layer | partial | Copernicus metadata parser/network and footprint records | Actual Copernicus layer source records plus dedicated radar catalogue/footprint checks | Historical | Broader manual source selection/timestamps and current Mac; metadata stays distinct from decoded pixels |
| Place search, route modes/endpoints/steps, annotations and drawing | partial | `Core/MapTools`, bounded Photon/OSRM, conflict-safe annotation library and native tool window | Nine core checks; actual Photon six-place search and OSRM foot route; native drawing/map/workspace persistence passes | Historical | Broader manual route modes and current Mac |
| Workspace saved state/import/export/view links | partial | `JsonFile` workspace I/O, private share-link fields, native URL admission and desktop scheme registration | Core privacy/unknown-field/date checks; native share-link dispatch retains local annotations, saves display state and restores pending follow | Historical | Actual OS association dispatch and legacy workspace round trip |
| Approximate solar terminator/day-night display | partial | `MapTools::SolarState`, 721-point terminator and native renderer settings | Core solar geometry checks and native display/workspace controls | Historical | Broader manual solar/date scenarios and current Mac; no precise ephemeris or measured illumination claim |
| Provider credential setup/storage/sign-out | partial | Optional Linux Secret Service/macOS Keychain, native provider setup; CDSE memory-only sessions | `secret-store.log`: actual temporary key save/read/delete; sign-out/generation tests | New adapter not executed on Mac | Actual optional provider keys and Mac Keychain checks; no plaintext fallback |
| Local model execution, hashes, box-to-map transforms and analyst review | partial | `Core/Inference`, `InferenceWindow`, CPU ONNX worker/contracts | 15 Python, four core, three UI checks; merged native acceptance/review/stale-source refusal; trained CPU run with zero proposals | New Mac CPU untested | Current Mac; no aerial accuracy claim; optional accelerators unverified |
| Linux installable launcher/archive/pacman artifact and external clean-profile launch | verified locally | `package-linux.py`, native launcher/user installer, package check and source/notices archives | Final R3 archive extraction/checksums/runtime/installer, native 104/live 19, actual optical 25 and FFmpeg media 3 pass outside checkout | Not applicable | Other distributions/hosted CI/public redistribution audit remain separate |
| Qt arm64 Mac bundle, signature, Metal and keychain | partial | Matching Qt deployment, worker copies, Mac notice collector; arm64 CI configured | Cannot execute on Linux | Historical Qt slice only | Current Mac/CI build, signature/dependency/license checks and actual Metal/hardware acceptance |

Intentional exclusions remain bike share, radio, cockpit, voice, San Francisco
neighborhood boundaries and direct ASI PRISMA ordering. All 19 enabled feed IDs
are represented: earthquakes, flights, military, atmosphere, disasters,
installations, energy, satellites, launches, vessels, fires, transit, cameras,
datacenters, dams, cables, sentinel, annotations and traffic. Daily visualization
is tracked separately. Optional acceleration is separate from Vulkan rendering.

## Data compatibility after removing the Swift client

The Swift reader/writer is gone, but the document format is unchanged and remains
supported:

- `Missions/plans.json` mission libraries, optical job folders and SAR job folders
  keep their existing fields. Qt preserves unknown extension fields, Foundation
  numeric dates, UUID references, review/monitor data and scientific values.
- Fixtures previously shared with Swift are now under
  `native/desktop/Tests/Fixtures/` and are exercised by the Qt core, optical,
  infrastructure, investigation and domain suites.
- Atomic saves, conflict checks, bounded parsing and backups remain in place.
  There is no second writer after this change, so the former cross-client locking
  caveat no longer applies; make a disposable copy before bulk migration anyway.
- Copied scientific products must still be imported explicitly. A mission evidence
  link never carries raster bytes.

## Next acceptance work

1. Exercise actual PRISMA/Sentinel-1 sources and SNAP GPT where external
   inputs/accounts/tools are available; broaden CBERS coverage/quality beyond the
   verified tiny window. Real GRD calibration/DEM and a compatible SAR review pair
   remain scientific acceptance gates.
2. Run configured hosted CI and current Apple Silicon Qt/Keychain/Metal
   regressions. Physical input, DPI/displays, accessibility, actual compositor
   minimization and large-site resource stress remain separate on both platforms.
