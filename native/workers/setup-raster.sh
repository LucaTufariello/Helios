#!/bin/bash
# Create a Python environment with the worker dependencies for local processing.
set -euo pipefail
helios_native="$(cd "$(dirname "$0")/.." && pwd)"

helios_repo="$(cd "$helios_native/.." && pwd)"
helios_sar_runtime="${HELIOS_SAR_RUNTIME:-$helios_repo/output/raster-venv}"

if [[ -e "$helios_sar_runtime" && ! -x "$helios_sar_runtime/bin/python3" ]]; then
  echo "The requested runtime directory exists but is not a Python environment." >&2
  exit 1
fi

"${HELIOS_SAR_PYTHON:-python3}" -m venv "$helios_sar_runtime"
"$helios_sar_runtime/bin/python3" -m pip install -r "$helios_native/workers/SAR/requirements.txt"
"$helios_sar_runtime/bin/python3" - <<'PY'
import platform
import rasterio
import subprocess

expected = subprocess.check_output(["uname", "-m"], text=True).strip()
assert platform.machine() == expected, (
    f"Python must run natively ({platform.machine()} != {expected})"
)
print("Worker raster environment ready; GDAL", rasterio.__gdal_version__)
PY
echo "Select this Python interpreter in the processing panels: $helios_sar_runtime/bin/python3"
echo "Install ESA SNAP separately with the Microwave Toolbox; select its bin/gpt in the processing panels."
