# Helios roadmap: defensive intelligence and infrastructure protection

Updated October 13, 2026. This is the proposed implementation order, grounded in
the current native application. Items below are planned unless explicitly marked
as existing. Milestones are acceptance gates, not promised calendar dates or
scheduled automations.

## Product goal

Make Helios a free, open-source, native macOS and Linux workspace that helps an
analyst understand an area, monitor important infrastructure, investigate changes,
coordinate a response and explain every conclusion with dated evidence.

The first complete operational use case is infrastructure monitoring: an analyst
selects a port, industrial facility, power installation or communications site;
builds a baseline; receives new evidence; reviews a possible change; records a
judgment; assigns follow-up work; and exports a reproducible briefing.

The central workflow is:

**Asset / area → baseline → monitoring → new evidence → reviewed finding → task → report.**

Public Palantir documentation describes linked operational objects and mission
plans connected to tasks, entities and resources. The architectural lesson for
Helios is to connect evidence and actions through persistent records. This
roadmap is an independent product proposal, not a claim to reproduce Maven's
complete capabilities or access to its data. See the primary references below.

## Existing foundation

Already available: native map/terrain/3D Tiles; grouped public data sources;
satellite acquisition libraries; Sentinel-1 GRD processing; optical imagery;
saved sites; bounded measured SAR change review; visual A/B comparison; and a
separate Mission Control window with linked evidence, notes, observations, tasks
and acquisition monitors. A local Core ML detector adapter exists, but real
remote-sensing model inference and accuracy still need validation.

These foundations should be extended. The roadmap does not reintroduce an old
website or replace working satellite processing. See [current state](CURRENT-STATE.md).

## Implementation sequence

| Priority | Milestone | Main additions | Dependency / completion gate |
| --- | --- | --- | --- |
| 0 | Cross-platform native foundation | Shared C++20 core, Qt desktop client, Metal/Vulkan rendering and optional hardware-specific inference | Existing documents/products work on both clients; real macOS and Omarchy/NVIDIA workflows pass; shared spatial module is the first implemented component |
| 1 | Reliable asset and evidence workspace | Infrastructure registry, unified evidence catalog, Data & Processing Center, source/quality inspector | Existing missions and libraries migrate safely; interrupted work recovers; every finding can identify its evidence |
| 2 | Validated site monitoring | Geographic/radiometric checks, reference evaluation cases, quality-aware comparisons, persistent review inbox | Measured change alerts require relevant quality checks; held-out evaluation reports false alarms and missed changes |
| 3 | Operational Mission Control | Investigation cases, task dependencies, response playbooks, decision history and briefings | Complete a documented infrastructure review from question to export with traceable decisions |
| 4 | Evaluated local assistance | Tiled aerial object proposals, analyst feedback, grounded local document/evidence assistant | Real model, licensing review and held-out evaluation on supported imagery; every AI claim cites evidence or remains explicitly unresolved |
| 5 | Connected domain and dependency analysis | Historical marine/air/ground observations, spatial event rules, infrastructure relationships and service-impact views | Replays preserve timestamps/gaps; links and impact assumptions are inspectable and correctable |
| 6 | Offline and unattended operation | Portable mission packages, storage budgets, opt-in platform background checks, resilient long-running jobs | Demonstrated disconnected workflow, sleep/relaunch recovery and bounded resource use |
| 7 | Team-ready open-source release | Optional collaboration, identity/permissions, durable audit, import/export adapters, distribution hardening | Two-user conflict/permission tests, recovery drills, clean-machine install and dependency/license review |

Persistence, security, accessibility, performance and scientific honesty are
requirements throughout. Milestone 7 adds multi-user capabilities; it does not
postpone basic security or release testing. Mission workflow improvements can
progress alongside scientific evaluation, but unvalidated measurements must not
silently become validated alerts.

## 1. Reliable asset and evidence workspace

### Infrastructure registry

- Give sites persistent identities, names/aliases, categories, boundaries,
  sub-assets, mission links and an analyst-specified monitoring purpose.
- Distinguish a physical asset from an observation about it. Retain contradictory
  source assertions and their dates; require review for ambiguous duplicate merges.
- Link existing saved sites, notes, radar products, optical images and records
  without copying large rasters or losing original identifiers.
- Add a site dossier: map, baseline imagery, latest evidence, known information
  gaps, notes, pending reviews and follow-up tasks.

### Unified evidence catalog and inspector

- Search/filter across missions by asset, area, sensor, acquisition date, source,
  processing state and review state. Synchronize map, table and timeline selection.
- Show acquisition time, retrieval time and processing time separately; use UTC
  internally and explicit display time zones.
- Track source identifier, footprint/CRS, pixel spacing, file integrity, processing
  parameters/version and derivation links from raw data to preview to finding.
- Show separate integrity, geographic, radiometric and corroboration states.
  “Unknown” is a valid state; one green badge must not imply all checks passed.
- Distinguish measured backscatter, visual comparison, model proposal and analyst
  conclusion in the inspector and exports.

### Data & Processing Center

- Consolidate downloads, optical retrieval, SAR processing and analysis jobs in a
  persistent queue with stage, elapsed time, diagnostics and output links.
- Support cancellation, bounded retries and restart recovery. Resume downloads
  only when the provider supports safe byte ranges and identity checks; otherwise
  restart explicitly. Do not promise resume for arbitrary SNAP stages.
- Add engine/environment preflight checks, disk-space checks, storage usage,
  missing-file recovery and safe deletion that explains dependent evidence.
- Apply memory/concurrency limits suitable for an 8 GB Apple Silicon Mac. Measure
  peak memory and map responsiveness using representative jobs before choosing
  default limits. Processing must stay off the UI thread.

**Acceptance:** create an asset from an existing site, attach existing imagery,
find it from either window, interrupt/relaunch a job and recover coherently.
Original data remains intact, old missions open, and failed jobs preserve useful
bounded diagnostics. No synthetic progress percentage or inferred acquisition time.

## 2. Validated site monitoring

- Implement registration checks with stable control areas, residuals in metres
  and source pixels, held-out verification and explicit rejection of bad matches.
- Expand radiometric checks with compatible independent processing, stable-area
  temporal variability and documented calibration limits. Keep Sigma0 and Gamma0
  distinct. Absolute accuracy requires suitable reference measurements.
- Assemble dated infrastructure change cases plus unchanged controls before
  tuning thresholds. Split sites/dates between development and evaluation;
  include difficult terrain, clouds and incomplete coverage as rejection cases.
- Attach quality reports to comparison results. Select compatible acquisitions,
  display cloud/nodata/terrain limitations and preserve correction parameters.
- Extend comparison beyond the current one-million-pixel cap through bounded
  tiled analysis with boundary/halo equivalence tests.
- Develop multi-date baselines and persistence checks after pairwise validation.
  A new acquisition and a measured change are different event types.
- Add a review inbox: new, investigating, corroborated, dismissed or unresolved;
  link evidence, rationale, owner and follow-up. Group duplicate events and let
  users acknowledge or snooze them. Priority reflects analyst-defined site
  importance and evidence quality, not an invented threat probability.

**Acceptance:** on held-out documented sites, publish registration error,
false-positive/false-negative results and known failure conditions. A failed
quality check prevents a validated-change claim. A review can be traced back to
the scientific rasters and processing settings. Numerical consistency alone is
not geographic or absolute radiometric validation.

Detailed scientific work remains in [the satellite validation plan](SATELLITE-ANALYSIS-PLAN.md).
Large earthworks and substantial footprint changes are initial candidates;
structural damage, activity/operational status and individual vehicles require
different evidence and evaluation.

## 3. Operational Mission Control

- Add investigation cases with a question, scope, relevant assets, baseline,
  evidence, competing explanations, findings and explicit completion criteria.
- Extend tasks with dependencies, checkpoints, ownership and linked findings.
  Initial ownership is local metadata; authenticated multi-user assignment comes
  with collaboration. Record changes to decisions and their rationale.
- Add reusable infrastructure inspection, outage investigation and post-event
  assessment playbooks. Each step specifies needed evidence and unresolved gaps.
- Add a chronological review with synchronized map/date filters and comparison
  selection, preserving the distinction between observation time and entry time.
- Generate a briefing with dated maps, findings, sources, uncertainty, open tasks
  and an evidence manifest. Export readable PDF plus machine-readable records.

**Acceptance:** complete an infrastructure investigation using real dated
evidence, reopen it after restart and reproduce its briefing. Selection in Mission
Control focuses the same object/evidence on the main map. Unsaved edits, missing
evidence and unresolved judgments remain visible.

## 4. Evaluated local assistance

- Benchmark a license-compatible aerial detector on the actual Mac and supported
  imagery before selecting a default. Validate real Core ML inference; add tensor
  or rotated-box decoding only for a chosen, tested model contract.
- Analyze full-resolution optical tiles with overlap, deduplication and correct
  coordinate transforms. Retain model version, input scale and source pixel links.
- Start with classes demonstrably resolvable in the imagery. Small-object proposals
  require suitably detailed data; Sentinel-2's 10 m pixels do not support arbitrary
  vehicle or small-building identification.
- Add accept/reject/correct actions and an evaluation dataset. Report per-class
  precision/recall, localization error, latency and memory. Model scores stay scores
  until calibration is measured. Feedback does not silently retrain a model.
- Add a local evidence assistant for questions such as “What changed at this site
  and which sources support it?” Start with retrieval, cited summaries and draft
  notes. Treat imported documents as untrusted data, keep permissions explicit and
  require user review before saving proposed operational actions.

**Acceptance:** a reproducible held-out benchmark and usable Apple Silicon resource
profile; proposals remain reviewable and separate from established facts. The
assistant exposes conflicting/absent evidence instead of filling the gaps.

## 5. Connected domains and infrastructure dependencies

- Persist bounded, permitted historical observations for marine, sky and ground
  sources with source time, receipt time, accuracy/freshness and coverage gaps.
- Add geographic watches around infrastructure: entry/exit/dwell events where
  observations support them, with identity uncertainty and missing intervals
  explicit. Do not equate a missing public report with disappearance.
- Connect sites, substations, communications links, roads and port facilities
  through imported or analyst-reviewed dependencies. Record provenance per link.
- Support service-impact investigation and response planning: which documented
  dependencies might be affected, what is unknown, and which inspection or repair
  tasks follow. Any scenario calculation states assumptions and uncertainty.
- Add authorized local sensor/status feeds behind replaceable connectors; verify
  schemas, timestamps, rates, terms and credentials for each deployment.

**Acceptance:** historical replay does not display stale observations as current;
entity links can be corrected; every event and dependency path reveals its source.
Demonstrate one infrastructure incident linking satellite evidence, relevant public
observations and an inspection plan without inferred identities or invented status.

## 6. Offline and unattended operation

- Package selected evidence and permitted map data with a mission manifest,
  integrity hashes, attribution and a clear distinction between included files
  and external references. Support import, verification and missing-file repair.
- Add cache quotas, archive/restore and tested backups. Preserve source rasters;
  deleting a preview should not delete scientific evidence by accident.
- Add opt-in native macOS background acquisition checks and notifications with
  visible scheduling, pause controls, battery/network policy and diagnostics.
  Explain when sleep/offline state prevents checks; avoid a catch-up download storm.
- Allow explicitly configured download/processing rules with disk, bandwidth,
  concurrency and daily limits. The default continues to require user selection
  before large acquisitions or processing.

**Acceptance:** open a complete packaged case offline, review its evidence, edit
notes and export a report. Test sleep/wake, network failure, full disk and relaunch.
No background task runs outside the user's configured policy.

## 7. Team-ready distribution and interoperability

- Add optional collaboration only after local workflows stabilize: shared cases,
  authenticated roles, scoped access, conflict handling and durable audit records.
  Keep the native client and offline single-user mode; a future sync service is
  optional infrastructure, not a browser replacement.
- Define the trust model before claiming tamper-evident evidence. Local hashes
  detect accidental file changes but cannot by themselves prove acquisition origin
  or protect against an administrator rewriting the entire history.
- Add versioned import/export contracts. Extend existing GeoTIFF/JSON support with
  prioritized GeoJSON/CSV and GIS exchange adapters such as GeoPackage based on
  concrete partner/test datasets. Maintain units, CRS, dates and attribution.
- Establish reproducible release builds, dependency inventories, model/data license
  manifests, clean-machine onboarding, accessibility checks and signed/notarized
  macOS distribution when signing credentials are available.
- Add migration/rollback, permission, conflict, backup-restore and malformed-input
  tests. Optional server deployment must document maintenance and security costs.

**Acceptance:** two analysts can exchange/reconcile a case without silent loss,
unauthorized access is rejected and independently installed clients can reproduce
an exported review. The free local workflow remains useful on its own.

The shared Qt client now has [connected dossiers](SITE-DOSSIERS.md) and a
[site monitoring workbench](SITE-MONITORING.md), including relative correction
and an inbox. These implementations do not complete real-data or fresh Mac
acceptance. The historical ordering below remains context for those gates.

## Next implementation batch

First establish [milestone 0](CROSS-PLATFORM.md) on both target platforms. Shared
spatial calculations now use C++20, and a separate Qt preview has GPU map/terrain,
saved satellite previews and mission workflows tested on the Mac. Linux hardware
acceptance and remaining desktop parity are still outstanding. Then start milestone 1 with
three reviewable deliveries, in this order:

1. **Asset dossier and evidence inspector:** extend saved sites into infrastructure
   records, link existing evidence and expose source/time/quality details. Validate
   storage migration, deletion behavior and selection shared between both windows.
2. **Data & Processing Center:** unify job visibility and preflight checks; persist
   recovery state; exercise interruption, cancellation, offline and disk failures.
3. **Monitoring baseline and quality report:** save an asset's reference acquisitions
   and stable controls, then attach the first registration and radiometric checks.
   Use a real documented site to begin milestone 2 evaluation.

This yields a useful site dossier immediately and a sound basis for subsequent
automated review. It does not depend on finding a universal detection model.

## Engineering and UX rules for every milestone

- Keep the full map as the primary workspace and Mission Control as a separate
  linked window. Use consistent inspectors, selection and contextual actions;
  avoid a new floating panel for every backend capability.
- Preserve the functioning macOS client during the transition to a shared C++/Qt
  desktop application. Use Metal on macOS, Vulkan on Linux and optional isolated
  scientific workers. Bound raster windows, model tiles, caches and work queues.
- Reuse current storage with versioned migration and backups. Introduce a spatial
  and text index, or SQLite, only when measured history/search scale warrants it;
  keep stable IDs and portable export independent of storage implementation.
- Separate source assertions, measurements, model proposals and analyst decisions.
  Keep provenance, date, uncertainty and data gaps visible in maps and briefings.
- Require relevant native/raster regression tests, real workflow checks, error and
  recovery tests, accessibility/keyboard checks, and target-Mac memory/latency
  measurements. Publish validation scope rather than a general “validated” claim.
- Test real dated reference data separately from generated fixtures. Define the
  evaluation protocol before tuning and report unsuccessful cases as well as wins.
- Keep credentials in Keychain, redact diagnostics/exports, validate untrusted
  archives and models, and review data/model redistribution terms at integration.

## Boundaries and deferred work

Free public observations have coverage, latency, licensing and resolution limits.
The product should show these limits and accept authorized local data. It cannot
promise continuous global sub-metre imagery or complete aircraft/vessel visibility.
Open-source application code does not make every bundled dataset or model freely
redistributable; optional restricted datasets need clear separation and notices.

Defer automatic construction/damage classification until evaluated; SLC/InSAR,
coherence and deformation until the GRD workflow is reliable; and broader PRISMA
spectral analysis until a concrete use case and accessible products justify it.
Direct ASI ordering stays deferred by the user's decision. Flood/water monitoring
can be an optional later workflow; infrastructure protection is the initial focus.

No roadmap task reinstates bike tracking, radio, cockpit or voice features.

## References and maintained detail

- [Current native capabilities](CURRENT-STATE.md)
- [Mission Control and current persistence limits](MISSION-CONTROL.md)
- [Satellite validation backlog](SATELLITE-ANALYSIS-PLAN.md)
- [Existing SAR checks and outstanding validation](SAR-QUALITY-AND-MONITORING.md)
- [Local detector constraints and evaluation](LOCAL-OBJECT-DETECTION.md)
- [Optical source access and resolution](OPTICAL-SATELLITE-SOURCES.md)
- [Data source terms](../DATA_SOURCES.md) and [security](../SECURITY.md)
- [Palantir Defense OSDK: mission planning](https://www.palantir.com/docs/defense-osdk/api/missionPlanning/overview/about)
  — primary public description of plans, tasks, entities, resources and links;
  consulted October 12, 2026. Used for high-level architectural context only.

## Connected investigations implementation update — October 20, 2026

The Qt client now connects shared asset identities, evidence-backed relationships,
approved geographic/identifier associations, reviewed findings, dependent tasks
and native briefing export. Cases keep adopted versions when the registry changes;
imports and updates require an explicit preview/adoption action. This implements
the connected case workflow across parts of milestones 1, 3 and 5. It does not
complete service-impact modeling, authenticated collaboration, quantitative optical
analysis, additional satellite-provider integrations or real monitoring accuracy
evaluation. Fresh macOS execution remains a release acceptance gate.

See [the workflow guide](CONNECTED-INVESTIGATIONS.md) and the investigation
acceptance record in `docs/evaluation/CONNECTED-INVESTIGATIONS-ACCEPTANCE.md`.
