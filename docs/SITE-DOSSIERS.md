# Connected site dossiers

The shared C++/Qt client on Linux and macOS has a dossier for each area in the
selected mission. Open **Mission Control → Areas** (Ctrl+6 on Linux), then choose
an area in the searchable list. The area's UUID is its dossier identity. Nothing
from another mission is implicitly included.

## Start a dossier

Choose an existing area, expand **New area** to name and draw a boundary, or use
**Import saved site** to copy a site from Infrastructure review's open site
library. **Infrastructure review → Open mission dossier** performs the same
import. Importing the same saved site again opens its existing dossier.

An imported site becomes a frozen mission evidence snapshot and a new area.
Renaming or moving the site in the global library does not update that snapshot.
Edit the mission area's boundary or purpose explicitly in Mission Control.

## Work through the dossier

- **Overview** shows linked evidence, pending observation reviews (Unreviewed or
  Uncertain), open tasks, dated evidence, pinned reference acquisitions and missing
  local data. Counts describe saved work; they do not establish readiness or quality.
- **Evidence → Suggested nearby** offers overlapping mission snapshots and imported
  local optical/radar products. Overlap is a discovery aid, not proof of usable
  coverage. Select an item and **Link suggested item**, or **Link mission evidence**
  to choose another snapshot deliberately. Suggestions never change the mission.
- **Evidence → Linked evidence** shows retained sensor/source/time metadata,
  availability, coverage warnings and bounded local previews. **Pin reference**
  marks a linked optical or radar acquisition as an analyst-selected baseline.
  Several references can be retained. Site, record, annotation and map-view
  evidence can be linked but cannot be raster baselines.
- **Compare with reference** carries the chosen evidence and another pinned raster
  into Timeline. Check its acquisition compatibility and warnings, then choose
  **Show / save visual comparison**. This uses the existing A/B map workflow.
- **Measured SAR review** carries the mission boundary and an available pinned
  radar acquisition into Infrastructure review. Choose the other calibrated
  acquisition and verify settings before starting the existing measurement job.
- **Reviews** holds area-assigned analyst/model observations. Add an observation
  with a verified coordinate and optional linked evidence, or assign an existing
  mission observation. Review as Unreviewed, Corroborated, Uncertain or Dismissed
  with a rationale. **Create follow-up task** retains that observation's identity.
- **Notes** holds area-assigned context, evidence references and citations.
  **Tasks** holds follow-up work, completion and optional deadlines through its
  editor. Existing mission entries can be reassigned explicitly to this area.

**Show area on map**, **Show selected on map**, **Find optical imagery** and
**Find radar acquisitions** open the existing shared tools. Imagery searches are
prepared for the area and last 14 days; the analyst starts the search/download.
No imagery retrieval, processing or automated interpretation starts merely by
opening a dossier. A visual preview, pin or analyst review state does not certify
registration, radiometry, geolocation or corroboration.

## Monitoring

Each dossier now also has a **Monitoring** tab for paused-by-default watches,
stable fitting/verification controls, local scientific analysis and run history.
The mission-wide **Review inbox** retains acquisition events, quality outcomes
and review follow-up. See [site monitoring](SITE-MONITORING.md) for setup and limits.

## Saving and cleanup

Dossier mutations enter the current mission library's pending changes. Use
**Save changes** to persist them, or **Save as** for a separate portable copy.
Existing atomic saves, conflict checks and last-good backups apply. Source raster
files are referenced through imported libraries and are not embedded in JSON.
If a product is absent on another machine, its retained metadata and reference pin
remain visible; import the complete product to restore the local preview.

**Unlink from dossier** removes only that area's link and reference pin. The item
remains in mission Evidence and its source library. Removing evidence from the
mission clears all affected dossier links/pins, source-site references and existing
note/observation evidence references. Removing an observation clears its task-origin
references while retaining the tasks. Removing an area retains notes, observations
and tasks as unassigned mission entries and removes its existing area monitors.

Changing area geometry retains curated links and pins, recomputes suggestions and
warns about evidence outside the new boundary or with unknown coverage. It does not
silently select replacement baselines. Dialogs reject a save if their mission or
area context changed; the text remains available in the open dialog.

## Portable schema and client compatibility

Mission brief version 1 remains in use. Areas may optionally contain:

```json
"dossier": {
  "version": 1,
  "linkedEvidenceIDs": ["<mission evidence UUID>"],
  "baselineEvidenceIDs": ["<linked optical/radar evidence UUID>"],
  "sourceSiteEvidenceID": "<linked site evidence UUID>"
}
```

The source-site field is optional. Both arrays are unique, bounded to 500 entries,
scoped to the same mission, and checked for dangling references. Baselines must
be linked raster evidence. Tasks may optionally contain `observationID`, referring
to an observation in the same mission. Legacy areas without dossier metadata remain
readable and start with no curated links or pins; overlap does not promote evidence.

The dossier metadata is optional typed persistence with validation and removal
cleanup. Concurrent editing by multiple writers is unsupported; fresh Apple
Silicon Qt/Metal acceptance still requires a Mac.

## Validation

`make test-desktop` covers schema validity, legacy documents, shared JSON fixtures,
mission isolation, deduplication, frozen imports, cleanup, round trips, boundary
edits, review/task origin and cancelled/stale dialogs. Projection/availability
checks run on copied metadata in a background worker; preview decoding has one
active worker and one latest pending selection. Stale results cannot replace the
current dossier. Suggestions are capped at 500, and no polling/render loop is added.

The generated native GPU validation in the [Qt guide](../native/desktop/README.md)
exercises dossier navigation, reference pinning, observation review, follow-up,
Timeline/SAR context and save/reopen. It retains `dossier-overview.png`,
`dossier-evidence.png`, `dossier-evidence-source.png`, `dossier-reviews.png`, `dossier-compact.png` and
`dossier-mission.json`. These use explicitly generated inputs. Document-compatibility checks use
`cross-platform-dossier.json` alongside the unchanged legacy fixture.
