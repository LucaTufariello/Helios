# Desktop workspace design

The Qt client keeps the native workspace organization: a persistent map,
Map / Satellite / Marine / Sky / Ground navigation, floating analysis palettes,
and a separate Mission Control window connected to the same working data.
The Qt visual system is applied to both its Linux and macOS builds; fresh Apple
Silicon execution remains an acceptance check.

## Map workspace

- Search places or enter `latitude, longitude` in the map search field. Coordinate
  navigation works offline. Place lookup still requires its public provider.
- Open **Satellite** for the saved imagery library and **Find imagery** for
  Processing Center. Processing, infrastructure review and local image assistance
  remain dedicated work surfaces.
- **Marine**, **Sky** and **Ground** filter sources in the floating sources panel.
  **Application menu → View → Data sources** shows every source. Its Sources,
  Records and Inspector tabs share the same selection. Activate a record to open
  its inspector. **Inspect** opens the inspector directly; **Data** opens its raw
  record tab.
- **Layers** contains basemap, terrain, display style, tilt and heading. **Tools**
  contains places, directions, annotations, daily imagery and workspace files.
  Display choices stay synchronized between these surfaces.
- Drag a panel's header to move it; drag its bottom-right handle to resize it.
  Collapse and close controls preserve its contents. A collapsed drawing panel
  suspends map point capture until it is expanded. Panel layout menus restore the
  default size and position; the application menu can reset all palettes.
- **Map** or **Hide all panels** clears the work surface. Panels remain bounded
  inside the map after resizing. When notices reduce the available height,
  controls scroll instead of overlapping. Layout positions and sizes are saved
  in the profile's `desktop.ini` after a successful normal quit.

## Mission Control

Mission Control has a mission selector and sidebar for Overview, Areas, Evidence,
Timeline, Objects, Notes, Tasks, Automations and Activity, matching the macOS
organization. Overview cards navigate to the corresponding working section.
Library actions live in the header's menu. The header retains **Show map** and
**Save changes**; creating the first mission has a dedicated empty state.

The Automations section exposes the existing acquisition/evidence monitors.
Source records remain available through disclosures in the timeline, evidence
and automation pages. Names, underlying mission JSON, save/conflict handling,
draft resolution and review states retain their existing behavior.

**Areas** now contains a searchable master list and a connected dossier with
Overview, Evidence, Reviews, Notes and Tasks. Curated links, pinned acquisitions
and task origins persist in optional mission metadata. Existing map, Timeline
and SAR tools receive the selected context. See [site dossiers](SITE-DOSSIERS.md)
for workflow, cleanup and compatibility details.

## Linux keyboard access

| Shortcut | Action |
| --- | --- |
| Ctrl+1 | Map; hide palettes |
| Ctrl+2 | Satellite analysis |
| Ctrl+3 / Ctrl+4 / Ctrl+5 | Marine / Sky / Ground |
| Ctrl+6 | Mission Control |
| Ctrl+K | Focus and select the map search field |
| Ctrl+Shift+H | Hide all palettes |
| Escape | Cancel an active map region selection |
| Ctrl+S | Save the mission library |

Controls retain keyboard focus indicators and accessible names. The compact
header hides its subtitle and source count, and uses an accessible Mission
Control icon, preserving the domain labels at 1080 × 700 logical pixels.

## Visual system and performance

`native/desktop/UI/WorkspaceStyle.cpp` owns the application palette, typography,
control states and vector icons. Canvas and panel surfaces use dark slate/navy;
muted cyan identifies selection and primary actions. Secondary text, warnings,
disabled controls and focus outlines have distinct treatments. The interface
does not imply security classification, connectivity or operational readiness
through decorative status badges.

The workspace uses native Widgets above the existing QRhi map. It introduces no
browser, blur effects, full-map translucent input layer or animation/render loop.
Only a panel's occupied rectangle intercepts map input. Scientific provenance,
source attribution and display-style limitations remain visible in context.

## Verification

`make test-desktop` includes workspace interaction checks for uncovered map
input, panel closing/reopening, retained drafts, bounded dragging/restoration,
collapse and scrolling in a short viewport. The existing native GPU validation
also exercises the new navigation, coordinate search, palette embedding,
display synchronization and Mission Control sidebar.

Use the validation commands in the [Qt guide](../native/desktop/README.md).
They save `design-map.png`, `design-satellite.png`, `design-sources.png`,
`design-layers.png`, `design-tools.png`, `design-compact.png` and
`design-mission.png` beside their reports. Generated validation products are
fixtures, not real observations. `--validate-live` captures actual public map,
terrain and daily visualization with the same interface. This does not replace
fresh Mac, physical input/display or accessibility acceptance.
