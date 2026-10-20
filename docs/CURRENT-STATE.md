# Current native application

GodEye has a functional Swift macOS client and a C++20/Qt client for the
macOS/Linux migration. The browser implementation is retired; neither client
uses a local Node service, browser UI or QML runtime. Swift retains SwiftUI/AppKit,
Metal and Foundation. Qt uses Widgets, native networking and RHI rendering, with
shared spatial/scene code and the existing native SGP4/SDP4 and scientific workers.

Qt now implements the enabled source layers, map tools, optical/SAR processing
controls, infrastructure review, Mission Control timelines/monitors and optional
local CPU assistance. It builds on Omarchy and passes native/core, generated
workflow and public-source Vulkan checks on the GTX 1070 in Wayland. Three small
generated scene/two-overlay travel cycles and owned-code sanitizer checks also
pass. Linux packaging includes the matching Qt and native FFmpeg runtime.
Fresh Mac/Swift/Keychain/Metal execution, real SNAP/calibration, keyed providers,
physical input/displays/accessibility and large-site stress remain acceptance
gates. The historical Mac preview check does not validate the expanded client.
Keep the Swift reference available; see the [platform transition](CROSS-PLATFORM.md),
[Qt guide](../native/desktop/README.md) and [migration status](MIGRATION-STATUS.md).

The Qt workspace follows the Swift reference navigation and floating-palette
layout, with a shared dark analyst theme and a separate Mission Control sidebar.
See the [desktop design guide](DESKTOP-DESIGN.md) for navigation and validation.

## Available workflows

- Full-map navigation with terrain/3D Tiles, loaded-surface picking, tilt/heading,
  bounded imagery caches, directions, annotations, record inspection and raw data.
- Public source groups for aircraft, AIS vessels, orbital estimates, launches,
  traffic, infrastructure, weather, earthquakes, disaster alerts and thermal detections.
- Canonical bundled datacenter/dam/cable data and public GTFS-realtime registry.
- Dated optical search/viewing, Sentinel-1 GRD discovery/download and optional local
  SNAP processing, floating-point exports, previews and terrain-aligned raster overlays.
- Saved site boundaries, compatible SAR acquisition pairs, measured-change proposals
  and analyst review decisions with evidence references.
- A separate Mission Control window with areas, evidence, timelines, observations,
  notes, tasks, acquisition checks, comparisons and portable local documents.
  [Area dossiers](SITE-DOSSIERS.md) connect curated evidence, pinned references,
  reviews and follow-up tasks to the existing map, Timeline and SAR tools.
- Site monitoring with persistent acquisition/analysis inbox entries, independent-control
  relative translation checks, derived corrected power rasters and analyst follow-up.
  See [monitoring](SITE-MONITORING.md). Real-data detection accuracy remains unevaluated.
- Optional local model proposals with explicit model/source provenance and review.
- Dated daily visualization, approximate solar terminator, annotation drawing and
  portable workspace/view links. Scientific acquisitions remain distinct from
  daily mosaics and display shaders.

The Qt source palette covers all 19 enabled layers and shares the map with mission,
processing, infrastructure and local-assistance windows. See [Mission Control](MISSION-CONTROL.md),
[map workspace](MAP-WORKSPACE-AND-MISSIONS.md), [Qt sources](../native/desktop/DOMAIN-SOURCES.md),
[map tools](MAP-TOOLS.md), [daily imagery](DAILY-IMAGERY.md) and
[CPU inference contracts](LOCAL-INFERENCE.md) for behavior and limits. Older
workflow guides describe the Swift reference where their interface differs.

## Current limits

The platform transition is still undergoing acceptance. The native clients'
current limits include:

- Triangle photogrammetry support rather than complete 3D Tiles/material coverage;
  same-origin HTTPS/local-directory scene references. Additional authenticated
  providers, calibrated camera projection and broader camera catalogs remain future work.
- Terrain clearance applies to loaded geometry. Mixed terrain datums/vintages and
  basemap seams prevent survey-grade or safety-critical navigation claims.
- SAR GRD processing requires external SNAP/raster tools; SLC/InSAR is not implemented.
  Independent radiometric/geographic validation is still required. Measured site
  comparisons currently have a one-million-source-pixel cap.
- Optical imagery has sensor-specific spatial resolution and coverage limits;
  enlarging pixels creates no extra detail. Direct ASI PRISMA ordering is not integrated.
- Visual A/B blending does not perform image registration or event detection.
  Model scores are not calibrated probabilities of physical events.
- Qt local assistance offers CPU ONNX execution only. A pinned trained VOC model
  has executed on a public optical preview, with zero proposals at the tested
  threshold; this is not aerial detector validation. Weights are not bundled or
  downloaded automatically. Optional NVIDIA/AMD inference remains unverified.
- Automations run while the app is open. There is no background daemon, cloud
  collaboration service or collaborative audit log.
- Provider terms, credentials, quotas, delayed reports and unavailable coverage
  constrain source access. Missing observations remain unknown.

Bike share, radio, cockpit, voice and San Francisco neighborhood boundaries are
outside the native product scope by the owner's request.

## Maintenance

Use the native [architecture guide](NATIVE-MACOS.md), [Qt guide](../native/desktop/README.md),
[testing instructions](../TESTING.md)
and [data source notices](../DATA_SOURCES.md). Native storage compatibility and
backup code remain in place for existing missions and downloads. Retiring the
website does not delete user data in Application Support or Keychain. Qt uses a
separate platform profile or explicit `--profile DIRECTORY`; it does not silently
replace the Swift library. Linux credentials use optional Secret Service, with no
plaintext fallback.

## Connected investigations (October 20, 2026)

The shared Qt Mission Control now includes Investigation, Asset registry,
Connections and Findings pages, with explicit asset adoption, evidence association
approval, immutable case versions, retained review decisions, dependent follow-up,
and native A4 PDF / JSON briefing export. Existing mission/evidence identities are
preserved. See [Connected investigations](CONNECTED-INVESTIGATIONS.md).

Linux regression, native Vulkan workflow, bounded graph and briefing rendering
results are recorded in `output/qa/investigations-20261020/`. Swift parsing,
formatting and a Linux compatibility harness exercise the new persistence models;
that harness uses minimal existing-model stubs and does not replace a fresh Mac
application build/run. macOS execution remains an acceptance gate. Generated
civilian evaluation records demonstrate workflow behavior, not real facility
changes or satellite-analysis accuracy.
