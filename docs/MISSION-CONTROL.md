# Native mission control

The shared Qt client implements this workflow on Linux and macOS; see
[connected site dossiers](SITE-DOSSIERS.md) and the
[desktop guide](../native/desktop/README.md). Evidence dated before November 2026
was collected with the earlier Swift reference and is retained as history.

Mission Control (Command-6) opens a separate, resizable macOS window. It shares
Helios's existing map, imagery libraries and selected mission. Move it to another
display if useful; **Show map** returns to the geographic context.

## Workspace navigation

The map remains the main workspace. The top bar groups Map, Satellite, Marine,
Sky and Ground; **Mission Control** opens the separate mission window. Labels on
**Layers**, **Inspect** and **Data** make the map tools discoverable. Floating panels
can be moved, resized, collapsed or closed; their **…** menu resets their size and
position. Selecting a panel brings it forward. Satellite and setup panels retain
minimum widths so their controls remain readable.

In **Satellite → Library**, search saved sites and acquisitions by name, sensor,
date or source identifier. Optical entries have bounded, asynchronously loaded
thumbnails. Opening an entry selects that acquisition and focuses its map extent.
Use **Find optical imagery**, **Open SAR workflow** or **Create a site** to add data.
The library shows stored jobs and their actual status, including failures.

Mission Control groups its sections into Plan, Analysis and Coordination. Its
**Overview** starts with clickable counts for areas, evidence, unreviewed observations
and open tasks, followed by the first three open tasks in saved order. The brief
and map bookmarks remain editable below; working notes expand on demand. Counts
summarize stored items, not mission readiness or independently validated findings.
Shared semantic macOS surfaces support light and dark appearance.

## Start a review

1. Create a mission with **+**, enter its name, objective and working brief, then
   **Save changes**. Existing missions remain readable.
2. In **Areas**, name a boundary and its purpose. Enter coordinates, use a selected
   satellite region, or **Draw area on map**. Two map clicks return the boundary to
   the mission window. Save it. Existing satellite sites can also become mission areas.
3. Use an area's imagery action to open the existing satellite search tools. Download
   optical imagery or process a Sentinel-1 acquisition there. In **Evidence**, link
   saved optical/SAR results, sites, map positions or selected source records.
4. Select evidence to inspect its preview and metadata; **Show on map** activates it.
   Record snapshots retain their saved coordinates/time even if a live feed changes.
   Compare two saved raster layers with the A/B opacity slider. This is visual
   comparison; use **Measured SAR change review** for the existing measured workflow.
5. In **Notes**, write a dated note with an area, evidence link and source citation.
   Notes can be edited. In **Objects**, record an identification, pick its coordinate
   on the map and attach evidence. Review it as Unreviewed, Corroborated, Uncertain or
   Dismissed, with a rationale. These are analyst judgments, not validated detections.
6. In **Tasks**, add a next action and optional due date; complete or reopen it.
7. In **Automations**, create a paused check, run it manually to establish a baseline,
   then enable it. Review status and new acquisitions here; choose which to download.
8. Export the mission as JSON. It contains bounded snapshots, areas, notes, tasks,
   observations, model provenance, automation checkpoints and recent activity.
   Source raster files remain in their existing libraries and are not embedded.

Entry forms retain their drafts when moving between sections. Closing the window,
switching missions or quitting prompts for unsaved entries/brief edits. Editing
entries uses their own save buttons; the toolbar saves the main brief and comparison.
Removing a mission evidence link keeps its source files and dependent notes/observations,
clearing only their reference. Removing an area keeps notes/observations/tasks but
removes its monitors. Notes, tasks, observations and automations also have confirmed deletion controls.
A one-time backup protects the legacy mission library.

## What automations do

- **Sentinel-2 acquisitions:** query the public optical catalogue for the saved area,
  using its maximum tile cloud setting.
- **Sentinel-1 acquisitions:** query GRD IW VV/VH products intersecting the saved area.
- **Evidence availability:** check linked local raster previews and stored snapshots.
  Availability does not certify radiometry, geolocation or original download integrity.

Checks run only while Helios is open, including when the mission window is closed.
A 60-second scheduler admits at most two checks concurrently. Intervals are 1–168
hours. Reopening/sleep recovery performs one overdue check, rather than replaying
all missed intervals. There is no background daemon or macOS notification service.

Acquisition searches cover the last 14 days and use the existing catalogue limits
(40 optical results or one 20-result radar page). The UI warns if results are limited.
The first successful query establishes a baseline. Subsequent queries count newly
seen identifiers, retaining at most 2,000 per monitor. These are newly discovered
catalogue entries, not necessarily acquisitions made since the preceding check.
Failures retain the last successful checkpoint and never manufacture results.
Changing an area's geometry resets its baseline and discards in-flight results
for its former region. Summaries/checkpoints persist; the current result list is
session-only. Review opens a fresh catalogue search. No large product downloads
or scientific processing start automatically.

## Optional local object detector

Manual evidence-linked identifications work immediately. Model assistance requires
a separately supplied **Vision-compatible Core ML object detector** (`.mlmodel`,
`.mlpackage` or `.mlmodelc`) returning recognized objects with bounding boxes.
No trained model, weights download or accuracy claim is bundled.

The adapter uses Apple Vision/Core ML on a background task with all available compute
units. It analyzes a linked georeferenced optical preview (at most 2048 pixels per
side and 32 MB encoded). Full-image `scaleFill` input maps normalized lower-left
Vision boxes into the preview's EPSG:4326 bounds. Classifier-only/raw-tensor results
are rejected. At most 200 proposals above the selected model score are retained.

Each proposal records label, score, bounds, source evidence, acquisition time,
model filename, input transform and preview SHA256. It starts **Unreviewed** and must
be explicitly added to the mission. Scores are uncalibrated model outputs, not
probabilities of a correct identification. Model files are not embedded in exports;
retain the exact model separately for reproducibility. Inference with a real trained
remote-sensing detector has not yet been validated in this implementation.

Use a detector trained and evaluated for the source imagery. A 10 m Sentinel-2
preview cannot resolve small vehicles or individual small buildings; image
resampling/model processing cannot add missing detail. This adapter does not run
on SAR, infer structural damage, or supply a validated construction detector.
See Apple's [Vision image transformation API](https://developer.apple.com/documentation/vision/vncoremlrequest/imagecropandscaleoption).

## Architecture and storage

- `Core/Missions.cpp`, `Core/Monitoring.cpp`: bounded area/note/object/task/monitor
  models, review states, deterministic scheduling/deduplication and box georeferencing.
- `Core/Monitors.cpp`, `Core/Timeline.cpp`: in-app acquisition/evidence monitors,
  baselines, deduplication and bounded activity history.
- `UI/MissionWindow.cpp`, `UI/MissionEditor.cpp`: dedicated Mission Control window,
  imagery preview, metadata inspector, comparison and mission forms sharing the map model.
- `UI/TimelinePanel.cpp`, `UI/MonitorPanel.cpp`: timeline, comparison and monitor controls.
- `Core/Inference.cpp`, `UI/InferenceWindow.cpp`: replaceable local ONNX CPU adapter used
  for observation proposals; no Apple Vision/Core ML path exists.
- `Validation.cpp`: isolated opt-in native window/GPU integration checks.

Storage remains `~/Library/Application Support/Helios/Missions/plans.json`. The
mission library is capped at 8 MB, 200 plans, and per plan: 500 evidence links,
100 areas, 500 observations, 200 notes, 200 tasks, 50 monitors and 200 activity events.
Other shared JSON consumers retain their existing 1 MB default. Invalid/unreadable
libraries are preserved. Activity is a bounded working history, not a tamper-proof
or collaborative audit log. Provider credentials are not copied into mission exports.

## Validation

Run `make test-desktop`, `make test-raster`, and `make format-check`.
The desktop suites cover legacy migration, rich document limits, references,
corruption preservation, model scores, schedule recovery/deduplication and normalized
box orientation/invalid coordinates.

For opt-in application checks, run the native binary with
`--validate --qa-output <new-directory>` (plus `--raster-python` for generated
scientific and infrastructure work, and `HELIOS_INFERENCE_PYTHON` for generated
model proposals). It uses a disposable profile, exercises shared map actions,
review/persistence/exports, runs an evidence monitor and captures the sections. Window capture uses macOS
`screencapture` and requires screen recording access. Read `validation.json`; this is
programmatic window/model validation, not pointer-based interaction testing. It does
not independently validate satellite radiometry, image registration or model accuracy.

### Verified on 11 October 2026

- Native core suite: 157 passed, 10 opt-in live checks skipped, zero failures.
- The new live mission catalogue check was then run separately and passed with
  five Sentinel-2 and eleven Sentinel-1 acquisitions for its test area.
- SAR/optical processing suite: 56 passed.
- Release app: arm64 build and local signature verification passed. The native
  map smoke check rendered on Apple M2, loaded 43 tiles and 37 earthquake records,
  and reported no renderer/tile failures.
- Isolated release application validation: 24 checks passed, including native
  window reopening, area drawing/point selection, shared map/evidence state,
  linked notes, observation review, tasks, evidence automation, migration backup,
  persistence, export and unlink preservation. All eight sections were captured;
  the layouts were inspected. These are programmatic application checks, not
  pointer-based UI automation. Detector inference with a trained model remains
  unverified. QA report and screenshots are under `output/qa/mission-control-2026-10-11`.

## Evidence timeline and area gaps

Open **Timeline** in Mission control after linking imagery or source records.
Filter by mission area, evidence type, UTC date range or text. Unknown observation
times remain explicit; note dates are labeled **Note authored**, not acquisition
times. Source records and object observations use their own observation timestamps.
Views, sites and annotations remain contextual evidence rather than fabricated
observation events.

Select an area to inspect recent optical/radar acquisition counts, missing local
rasters and unknown times. The lookback window is independent of the visible list
filters. Counts deduplicate crops/reprocessing of the same acquisition. An area
matches rectangular preview bounds, point coordinates or an explicit area link;
this does not certify cloud-free coverage, valid pixels or scientific readiness.
**Find more imagery** opens the existing satellite tools for the area. **Create a
review task** records an area-linked follow-up without duplicating an open task.

Choose **Set A** on a baseline and **Set B** on a later acquisition, then **Compare
on map**. Both previews must exist locally, have known ordered dates, represent
different acquisitions and have overlapping bounds. Mixed sensors/bands or pixel
spacing differences are explained. The shared map uses the existing opacity blend;
this is visual review, not measured change or automatic image registration. **Save
changes** retains the active comparison with the mission.

New optical/radar evidence links retain a small acquisition metadata snapshot
(source identifier, sensor, acquisition time, bounds, display band and spacing).
This survives removal of the source raster. For older links, **Save acquisition
metadata** explicitly backfills available metadata from the local libraries;
missing metadata is never replaced with a link creation date. **Refresh** rechecks
local availability. No raster copies or remote requests are needed for the timeline.

**Export filtered CSV** exports the displayed entries with UTC dates, explicit time
basis, source identifiers, availability, evidence references, bounds and review text.
Spreadsheet formula prefixes and quoted text are escaped. The full mission JSON
also retains acquisition snapshots; neither export embeds imagery.

Implementation: `native/desktop/Core/Timeline.cpp` contains deterministic filtering,
assessment, comparison checks and CSV export. `UI/TimelinePanel.cpp` builds entries
from the existing stores, presents filters and controls the shared map. The core and
UI tests cover date boundaries, unknown times, area matches, acquisition deduplication,
missing files, comparison constraints, backwards-compatible snapshots and CSV.
See [Choosing a local detector](LOCAL-OBJECT-DETECTION.md) for model recommendations
and the current adapter's limits.

### Timeline verification on 11 October 2026

The extended native suite passed 165 tests with 10 opt-in live checks skipped and
zero failures, including eight new timeline tests. The rebuilt arm64 release passed
34 isolated application checks: acquisition metadata retention, comparison activation
and persistence, deleted-preview rejection/recovery, note time semantics, CSV export
and the existing mission workflows. All nine sections and a compact timeline window
were captured; the timeline layouts were inspected. Formatting, release build and
local signature verification passed. Reports and screenshots are
in `output/qa/mission-timeline-2026-10-11`. These use labeled generated fixtures;
they do not validate detector accuracy or physical/radiometric change.

## Connected investigations

Use Mission Control’s Investigation, Asset registry, Connections and Findings pages to retain shared infrastructure identities, review dated evidence, track dependent follow-up and export an A4 PDF with a JSON evidence manifest. See [Connected investigations](CONNECTED-INVESTIGATIONS.md) for the complete workflow and historical-record rules.
