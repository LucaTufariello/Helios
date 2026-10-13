# Current native application

The native macOS transition is complete and the browser implementation is retired.
GodEye uses SwiftUI/AppKit, Metal, Foundation, compiled Cesium Native and native
SGP4/SDP4 on Apple Silicon. It has no website, local Node service or browser runtime.

Cross-platform work has begun for macOS and Linux: the current client now uses a
shared C++20 spatial library for WGS84 conversion, camera basis and ray picking.
Its standalone build has no Apple dependencies. A separate C++/Qt preview now has
GPU map/terrain rendering, saved optical/SAR preview overlays and mission editing.
Metal execution and existing product viewing have been checked on the Mac;
Linux/Wayland/Vulkan validation, remaining feature parity and Linux release
packaging are still outstanding. The full macOS client remains available. See the
[platform transition](CROSS-PLATFORM.md).

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
- Optional local model proposals with explicit model/source provenance and review.

The map navigation, source controls, satellite library and mission overview use
shared native design components with light/dark appearance. See [Mission Control](MISSION-CONTROL.md)
and [map workspace](MAP-WORKSPACE-AND-MISSIONS.md) for everyday workflows.

## Current limits

Completing the platform transition does not make every possible provider, file
format or analytical feature available. The native app's current limits include:

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
- Automations run while the app is open. There is no background daemon, cloud
  collaboration service or collaborative audit log.
- Provider terms, credentials, quotas, delayed reports and unavailable coverage
  constrain source access. Missing observations remain unknown.

Bike share, radio, cockpit, voice and San Francisco neighborhood boundaries are
outside the native product scope by the owner's request.

## Maintenance

Use the native [architecture guide](NATIVE-MACOS.md), [testing instructions](../TESTING.md)
and [data source notices](../DATA_SOURCES.md). Native storage compatibility and
backup code remain in place for existing missions and downloads. Retiring the
website does not delete user data in Application Support or Keychain.
