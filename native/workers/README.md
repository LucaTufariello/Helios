# Scientific workers

The raster, optical and inference workers run as separate Python processes behind
the native process/job boundary. The Qt client never imports them in-process, so a
worker failure, cancellation or dependency problem cannot take down the desktop
application.

- `SAR/` — Sentinel-1 GRD admission and the SNAP GPT orbit/noise/calibration
  pipeline, registration and measured SAR change review (`sentinel1.py`,
  `registration.py`, `change_review.py`, plus the pinned requirement sets).
- `Optical/` — Sentinel-2 and local PRISMA/Sentinel-2 spectral handling
  (`sentinel2.py`, `optical_sources.py`).
- `Inference/` — the optional CPU ONNX worker.
- `tests/` — unit tests with generated fixtures. They are not operational satellite
  acquisitions.
- `validation/` — evaluation helpers used for scientific acceptance work.

Run the tests with `make test-raster`. Create an environment with the worker
dependencies first using `make setup-raster`, or point `HELIOS_SAR_PYTHON` at an
existing interpreter. SNAP itself is installed separately; select its `bin/gpt` in
the processing panels.
