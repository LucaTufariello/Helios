# Map workspace and mission planning

GodEye opens on the native Metal map with no fixed sidebars. The horizontal bar
contains **Map**, **Satellite**, **Marine**, **Sky**, **Ground**, and **Mission
planner**. Map sections open floating tool palettes; Mission planner now opens a separate native window. See [Mission control](MISSION-CONTROL.md) for the current mission workflow. The map remains
interactive outside palettes and remains the rendering surface for all sections.

- **Satellite:** SAR acquisition/download/processing, optical sources, saved library,
  and infrastructure change review. Library brings saved sites and acquisitions
  into one place. Existing deletion and export controls remain in their tools.
- **Marine:** AIS vessels and submarine cable systems/landing points. Existing
  provider credentials, data attribution and coverage limitations still apply.
- **Sky:** civil/military flights, orbital catalogs, launch metadata and atmosphere.
- **Ground:** infrastructure, energy, datacenters, dams, transit, cameras, road flow,
  disaster/thermal observations and annotations.
- **Mission planner:** locally saved investigation plans connecting named views,
  site boundaries, selected source records, radar acquisitions, optical products
  and user annotations. It does not issue operational commands or satellite tasking.

The upper-right controls open map/layer settings, inspection, raw data and the
existing workspace tools. Drag a palette's title bar to move it, use its lower-right
handle to resize, **−** to collapse, and **×** to close. The top bar's hide button
and **Map** section clear palettes without disabling sources or evidence layers.
Map/layer controls can explicitly remove an evidence overlay.

Keyboard shortcuts: Command-1 Map, Command-2 Satellite, Command-3 Marine,
Command-4 Sky, Command-5 Ground, Command-6 Mission planner. Existing navigation,
source refresh and view-link shortcuts remain available.

## Original map integration workflow

The separate mission window supersedes the mission palette described below; saved
views, record snapshots, comparison and export behavior remain supported.

1. Open Mission planner and press **+** to create a named plan.
2. Enter an objective. Name a location and choose **Save current map view**.
   Coordinates, range, heading and tilt are retained.
3. Open Satellite tools alongside the mission palette. Select a saved site,
   optical image or processed radar acquisition. In **Evidence**, link the selected
   item to the plan. Inspector can link selected infrastructure, cable or track
   records. Record snapshots retain source time, attribution and bounded geometry;
   they are distinct from subsequent live feed updates.
4. **Notes** holds mission notes and links to the existing map annotation tools.
   **Save** persists the current draft. Switching plans or closing the app prompts
   before discarding unsaved mission text. Merely closing a palette keeps its draft.
5. **Compare** selects two saved georeferenced optical/SAR preview layers.
   Enable comparison, focus A and move the A/B blend slider or choose Show A/B.
   The native renderer independently maps both layers onto the loaded terrain.
   Save retains the pair and blend with the mission; reopening the mission restores
   those choices, with display disabled until you enable comparison again.
6. Open **Raw data** to inspect a read-only JSON snapshot of a selected record,
   optical result, SAR job, change review or mission. Refresh captures the latest
   metadata. This shows the application's normalized records and processing
   metadata, not original provider responses or complete raster pixel arrays.
7. Export the plan as JSON. It contains evidence identifiers and site/record/view
   snapshots; acquisition files stay in their existing libraries. References to
   deleted imagery report missing data rather than substituting another image.

## Comparison meaning

The blend is a visual review aid, not a computed change detector. The layer ordering
is A below B; B opacity controls blending. Invalid/transparent preview pixels retain
underlying coverage. Non-overlapping bounds produce a visible notice. Native
previews retain their existing resolution limits and georeferencing; this feature
does not sharpen imagery or register independently misaligned acquisitions.
Comparisons between different sensors, dates, geometries or display stretches need
human interpretation. For measured SAR change, use the existing compatible-pair
infrastructure review workflow and its documented validation limits.

## Implementation and persistence

SwiftUI owns in-window palettes and native controls; the original Swift/AppKit tools
and source configuration remain reused. Metal renders at most two bounded raster
preview layers. Texture preparation is asynchronous and bounded to 2048 pixels per
side/32 MB encoded input; opacity changes reuse textures and geometry. Closing a
palette does not cancel a dataset processing job. Existing cancellations remain in
the SAR/optical tools.

`MissionPlan.swift` owns validated bounded mission documents. Atomic storage is
`~/Library/Application Support/GodEye/Missions/plans.json`, with an 8 MB document cap,
200-plan cap, 500-item cap per plan, 120-character names, 4,000-character objectives
and 32,000-character notes. Unreadable previous files are preserved. Numeric
Foundation dates retain subsecond timestamps; shared decoders also admit ISO date
strings. Mission exports omit provider credentials and transient camera media links.
Large raster products are linked by dataset IDs rather than copied or embedded.

`GODEYE_VALIDATION_ROOT` isolates missions, imagery, reviews and annotations for
native UI tests; production storage paths and files otherwise remain unchanged.
This version has local mission planning/export, not collaborative missions, remote
synchronization, automatic target classification or operational execution.

## Verification on October 5, 2026

The native suite passed 145 checks with nine opt-in provider checks skipped. Eight
new mission tests cover persistence, fractional observation timestamps, references,
site snapshots, invalid documents, corruption preservation and comparison bounds.
The 56 raster processing tests and existing 3,899 web regression tests also passed
(one legacy test and two Node-version-specific allocation benchmarks were skipped).
The arm64 release build, signature verification, formatting and module boundaries
passed.

Native UI checks used an isolated library of clearly labeled generated fixtures.
They covered initial map layout, grouped Marine/Sky controls, concurrent mission
and satellite palettes, palette drag/resize/collapse, saved views and evidence,
notes and map annotation links, raw JSON inspection, mission export, A/B blending,
comparison configuration restoration and two-corner satellite region selection.
These checks do not independently validate satellite radiometry or image alignment.
