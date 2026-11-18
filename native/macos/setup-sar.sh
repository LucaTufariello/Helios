#!/bin/bash
set -euo pipefail
helios_package="$(cd "$(dirname "$0")" && pwd)"
helios_sar_runtime="${HELIOS_SAR_RUNTIME:-$HOME/Library/Application Support/Helios/SAR/runtime}"
if [[ "$(uname -m)" != "arm64" ]]; then
  echo "Use an Apple Silicon Python interpreter on an arm64 Mac." >&2
  exit 1
fi
if [[ -e "$helios_sar_runtime" && ! -x "$helios_sar_runtime/bin/python3" ]]; then
  echo "The requested runtime directory exists but is not a Python environment." >&2
  exit 1
fi
"${HELIOS_SAR_PYTHON:-python3}" -m venv "$helios_sar_runtime"
"$helios_sar_runtime/bin/python3" -m pip install -r "$helios_package/Sources/HeliosMac/Resources/SAR/requirements.txt"
"$helios_sar_runtime/bin/python3" -c 'import platform, rasterio; assert platform.machine() == "arm64", "Python must run natively on Apple Silicon"; print("SAR raster environment ready; GDAL", rasterio.__gdal_version__)'
echo "Select this Python interpreter in the SAR panel: $helios_sar_runtime/bin/python3"
echo "Install ESA SNAP separately with the Microwave Toolbox; select its bin/gpt in the SAR panel."
