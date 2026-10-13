# Optical fixture provenance

- `cross-platform-mission.json`: a Qt-authored mission document captured from the isolated desktop validation harness on 2026-10-13. All sites, imagery, observations and notes are explicitly generated QA fixtures. Both C++/Qt and Swift read/write this document to check dates, geometry, evidence links and storage compatibility. It contains no user mission data or real imagery.

- `cbers-catalog.json`: compact, real public INPE metadata for `CBERS_4A_WPM_20261003_198_130_L4`, retrieved 2026-10-05. No satellite pixels are committed.
- `PRS_L2D_GENERATED_20260922102800_0001.he5`: generated 48 × 48 arithmetic PAN fixture with a WGS84 UTM test grid, non-default reflectance scaling, timestamp and an empty error mask. This is **not a PRISMA acquisition** and establishes no sensor accuracy. It is about 20 KiB; keep it confined to automated tests and clearly labelled isolated developer validation profiles.
- Existing `sentinel2-catalog.json` and `sentinel2-result.json` retain their original real catalogue/result metadata and verify backward-compatible Sentinel-2 documents.
