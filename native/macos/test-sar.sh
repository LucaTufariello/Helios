#!/bin/bash
set -euo pipefail
godeye_package="$(cd "$(dirname "$0")" && pwd)"
"${GODEYE_SAR_PYTHON:-$HOME/Library/Application Support/GodEye/SAR/runtime/bin/python3}" -m unittest discover -s "$godeye_package/Tests/SAR"
