# Site monitoring and verified relative correction

The shared Qt client now connects dossier references, stable controls, local SAR
analysis and a persistent mission review inbox. This is a relative-registration
and measured-backscatter workflow. It does not establish absolute geographic or
radiometric accuracy, identify construction automatically or infer operating status.

## Setup

1. Open **Mission Control → Areas**, select a dossier, link a completed radar
   product and pin its VV or VH output as a reference in **Evidence**.
2. Open **Monitoring → Setup**, choose that reference and matching polarization.
   Default analysis settings are 3 dB, a 5 × 5 linear-power mean and 25 connected
   pixels. Registration defaults are a 2-pixel maximum translation, 0.5-pixel
   maximum verification residual and 0.6 minimum correlation. These are provisional
   engineering settings, not calibrated uncertainty or detection probabilities.
3. In **Stable controls**, draw or enter at least three **Fit** and three **Verify**
   rectangles over independently judged stable, textured radar returns. Rectangles
   must not overlap. Each is sampled as a centred 64–256 pixel chip. Use dated
   evidence to justify stable areas; agricultural, water, snow and changing surfaces
   can invalidate the assumption. Verification controls are excluded from fitting.
4. Choose **Save setup**. Configurations start paused. With a saved library, this
   saves explicitly using the existing conflict checks and backup mechanism.
5. Import a suitable later scientific acquisition. **Run local analysis** performs
   a one-off run even while paused. **Enable this site's watch** enables automatic
   local analysis and its associated Sentinel-1 catalogue check. Global automatic
   checks can be paused in Automations; **Resume checks** resumes them. Opening a
   library with `--mission-library` keeps automatic checks paused.

Compatible pairs retain the existing same-satellite, IW/other supported mode,
pass-direction, relative-orbit, polarization and processing-setting checks.
Source manifest hashes and scientific file sizes/modification times are rechecked
when queued work begins; these file identities are not full-scene content hashes. Original
scientific Sigma0 files must be present; RGB previews cannot substitute for them.

Install the optional registration dependencies into the selected **raster Python**,
not the application's inference environment or system Python:

```sh
/path/to/raster/python -m pip install -r native/macos/Sources/GodEyeMac/Resources/SAR/requirements-monitoring.txt
```

This adds scikit-image 0.26.0 and its dependencies. The ordinary map, optical and
existing uncorrected SAR review do not require it. Missing registration imports
produce a blocked report with setup guidance; the application does not install
packages, change drivers or download a model automatically.

## What happens automatically

Monitoring runs while GodEye is open. The associated catalogue monitor uses the
saved interval (1–168 hours), existing 14-day query and bounded result page.
The first successful discovery check establishes its checkpoint; later newly
seen catalogue identifiers create acquisition inbox entries. Their acquisition
time and usable site coverage remain unknown until inspected. A new catalogue
identifier is not a measured change.

Enabled sites analyze suitable already-imported local rasters. Downloads and SNAP
processing remain user-started. Scientific work shares one queue with Processing
Center: one active worker and at most 20 pending requests. Manual processing is
prioritized and can cancel an active automatic analysis, waiting for its owned
worker session to stop. Cancel in Processing Center stops active and queued work;
individual analyses can be cancelled in the inbox.

Source identities, scientific file sizes/modification times, area boundary and configuration determine a run fingerprint.
Already handled pairs do not run repeatedly. Explicit **Run local analysis** can
retry blocked, failed or interrupted pairs; source jobs are retained, and retry
history is bounded. Changing settings or the area invalidates pending work.
Completed results remain traceable to their historical configuration. The inbox
holds 200 entries per mission and refuses additional entries when full; remove
resolved entries explicitly. No unresolved review is silently evicted.

## Correction and quality

The worker reads bounded windows, never an entire acquisition. It estimates a
translation from smoothed log-intensity fitting chips, checks texture, reversible
estimates, competing correlation peaks and agreement between fitting controls.
It uses the median fitting translation, then samples original later **linear
power** once onto the reference grid. Originals remain unchanged.

Independent verification chips are measured after correction. At least three
usable chips in each role and the configured residual/correlation limits are
required. Missing/failed verification blocks candidate generation. Common valid
coverage below 50% also blocks automatic measured results. Quality reports retain
unusable controls and reasons instead of replacing them with a success score.

Reports separate relative residuals in pixels/approximate metres, common coverage,
stable-control median dB differences and median absolute deviations. Stable-area
statistics are diagnostics, not proof of absolute calibration or physical change.
Sigma0 remains unflattened Sigma0; no terrain-flattened Gamma0, deformation,
coherence, affine/nonrigid correction or general-purpose construction classifier
is implied. Relative checks cannot establish the accuracy of the original geocoding.

Ready jobs retain `corrected-linear-power.tif`, the existing scientific change
outputs, `quality.json`, source fingerprints, controls, settings and library
versions. Blocked jobs retain their report and explanation and publish no change
candidates. Registration uses the documented
[scikit-image API](https://scikit-image.org/docs/stable/api/skimage.registration.html)
and bounded [Rasterio reprojection](https://rasterio.readthedocs.io/en/stable/api/rasterio.warp.html).

## Review and recovery

**Mission Control → Review inbox** filters the selected mission by site text,
source, event type and review state. Each dossier's **Monitoring → Run history**
shows its own entries. Open ready results in the existing measured candidate
review; inspect relative quality, then corroborate interpretations with dated
optical evidence and other sources.

Review entries as New, Investigating, Closed, Dismissed or Snoozed, retaining a
rationale. Snooze hides an entry from the active filter for 24 hours; All reviews
still shows it. These states do not confirm a physical event. Create a follow-up
task to retain a link to its originating inbox entry. Explicitly save manual
review/task changes with the mission toolbar.

Compact source/quality summaries, review rationale and job UUIDs travel in mission
JSON. Detailed rasters/reports stay in the job library. Missing local reports do
not erase portable summaries. Removing source evidence disables a watch using it
and clears dangling references; deleting an area retains inbox history as stale,
unassigned entries. Removing a resolved inbox entry retains its job files and
clears task-origin links. Restarted queued/interrupted runs require an explicit
retry; completed owned jobs can recover their result into the inbox. No saved PID
is used to signal a process.

Qt and Swift have matching optional typed persistence extensions. The Swift
reference preserves these records and cleans up removed references; its UI does
not implement this monitoring workbench. Use updated clients and separate working
copies. Fresh Swift/macOS execution remains a platform acceptance check.

## Civilian evaluation case

The [Gagarin development protocol](evaluation/gagarin-development.json) identifies
historical civilian airport construction; the [held-out protocol](evaluation/gagarin-held-out.json)
reserves separate acquisitions before threshold tuning. The official aviation
agency documents the airport's [2019 opening](https://favt.gov.ru/novosti-novosti/?id=7990)
and [April 2019 construction progress](https://special.favt.gov.ru/novosti-novosti/?id=5320).
These documents establish historical context, not pixel-level labels.

Public catalogue discovery on 2026-10-18 returned 13 Sentinel-1A candidates for
summer 2016 and 15 for summer 2020, without reaching the 100-result metadata cap.
The protocols retain exact candidate UUIDs, names, dates, geometry attributes and
byte sizes. Catalogue intersection alone does not prove that all stable controls
are covered. The acquisition candidates require manual download/processing and
independently dated optical labels before real-data accuracy can be evaluated.

```sh
.venv/bin/python3 native/macos/Validation/evaluate_monitoring.py \
  --case docs/evaluation/gagarin-development.json --discover \
  --report output/qa/gagarin-catalogue.json

.venv/bin/python3 native/macos/Validation/evaluate_monitoring.py \
  --case docs/evaluation/gagarin-held-out.json \
  --review /path/to/completed/job/product/result.json \
  --report output/qa/gagarin-held-out.json
```

Discovery retrieves metadata only. Author non-overlapping WGS84 GeoJSON polygon
labels in the protocol's `labels` array with `class` (`changed`, `unchanged`,
`uncertain`), `geometry`, `sourceURL`, `referenceDate` and `rationale`. Record labels
before tuning. Evaluation reports labelled valid-pixel precision, recall, false
positives/negatives, excluded uncertainty and invalid support. Both changed and
unchanged independent reference pixels are required; absent labels/data return
`pending` with no accuracy metrics. A single-site result does not establish
performance at other sites, seasons or geometries.

## Verification

`make test-desktop` includes monitoring persistence, cleanup, queue limits,
manual priority, setup drafts, stale review dialogs and shared Swift/Qt fixtures.
With the optional environment installed, `native/macos/test-sar.sh` additionally
checks fractional shifts, independent deformation rejection, ambiguous/invalid
controls, original preservation, retained real change, source changes and labelled
generated evaluation. Set `GODEYE_SAR_PYTHON` to that environment's interpreter.

The native GPU validation exercises setup, controls, corrected analysis, blocked
verification, deduplication, inbox/candidate handoff, save/reopen, missing reports,
compact layouts and idle map rendering. It retains `monitoring-setup.png`,
`monitoring-controls.png`, `monitoring-inbox.png`, `monitoring-review.png`,
`monitoring-compact.png` and `monitoring-mission.json`. Its scientific inputs are
explicitly generated; an unavailable optional environment is recorded as skipped.
Real SNAP/calibration evaluation, fresh Mac hardware and large-site stress remain
separate acceptance checks.
