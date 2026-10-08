# Satellite analysis plan

Updated October 4, 2026. The following validation work is planned for the next working session, October 5. This is a project plan, not a scheduled automation or a claim that the checks are implemented.

## Next session: validate infrastructure change review

1. **Real evaluation cases:** choose a documented, substantial industrial/logistics construction or expansion on relatively flat terrain. Retain dated reference evidence and nearby stable control areas. Collect multiple comparable before/after Sentinel-1 acquisitions; keep changed, unchanged and uncertain labels separate.
2. **Pair registration:** measure offsets across stable areas, report east/north residuals in metres and analysis pixels, verify on held-out controls, and reject unreliable matches. Any correction must retain its parameters and original imagery. A residual below half an analysis pixel is a provisional engineering goal, to be evaluated rather than advertised as current accuracy. Grid resampling alone is not registration validation.
3. **Radiometric consistency:** expand independent source calibration checks, measure stable-area temporal variability and compare compatible quantities against an external processor. Preserve the distinction between unflattened Sigma0 and terrain-normalized Gamma0. Absolute calibration requires reference measurements with known uncertainty; another processor alone does not certify it.
4. **Event evaluation:** label reference changes before tuning thresholds. Report corroborated findings, false alarms, missed changes and persistence across later dates. Separate tuning sites/dates from held-out evaluation cases. No calibrated probability or automatic damage/construction claim until supported by evaluation.
5. **Native quality panel:** show separate numerical-integrity, registration, stable-control and reference-corroboration results, with measured errors, timestamps, sources and unavailable/failed states. Attach these results to portable review evidence.
6. **Large-region processing:** replace the one-million-pixel review cap only after implementing bounded tiles with averaging halos, cross-tile connected-patch merging and tests against the existing single-window result. Larger coverage does not establish greater analytical accuracy.

Start with registration measurements and stable controls alongside one independently documented infrastructure case. Then tune the detector using the measured error/variability baseline. Add terrain flattening, layover/shadow/incidence quality masks and multi-temporal baselines before broad mountainous or mixed-geometry deployment.

See [current review workflow](SATELLITE-INFRASTRUCTURE-REVIEW.md) and [existing SAR numerical checks](SAR-QUALITY-AND-MONITORING.md).

## Current session: dated Sentinel-2 optical context

Implemented public Sentinel-2 Collection-1 L2A search and true-colour viewing inside Satellite → Reference imagery ([guide](SENTINEL2-OPTICAL.md)). Request a region/date range/cloud threshold, select an acquisition, retrieve COG image blocks and display a georeferenced native overlay. Retain acquisition metadata and export the bounded rendered image. This adds optical evidence context; it does not implement the validation/detector work above.
