#!/bin/bash
set -euo pipefail
godeye_package="$(cd "$(dirname "$0")" && pwd)"
godeye_sar_runtime="${GODEYE_SAR_RUNTIME:-$HOME/Library/Application Support/GodEye/SAR/runtime}"
if [[ "$(uname -m)" != "arm64" ]]; then
  echo "Use an Apple Silicon Python interpreter on an arm64 Mac." >&2
  exit 1
fi
if [[ -e "$godeye_sar_runtime" && ! -x "$godeye_sar_runtime/bin/python3" ]]; then
  echo "The requested runtime directory exists but is not a Python environment." >&2
  exit 1
fi
"${GODEYE_SAR_PYTHON:-python3}" -m venv "$godeye_sar_runtime"
"$godeye_sar_runtime/bin/python3" -m pip install -r "$godeye_package/Sources/GodEyeMac/Resources/SAR/requirements.txt"
"$godeye_sar_runtime/bin/python3" -c 'import platform, rasterio; assert platform.machine() == "arm64", "Python must run natively on Apple Silicon"; print("SAR raster environment ready; GDAL", rasterio.__gdal_version__)'
echo "Select this Python interpreter in the SAR panel: $godeye_sar_runtime/bin/python3"
echo "Install ESA SNAP separately with the Microwave Toolbox; select its bin/gpt in the SAR panel."
