#!/bin/bash
set -euo pipefail
helios_package="$(cd "$(dirname "$0")" && pwd)"
"${HELIOS_SAR_PYTHON:-$HOME/Library/Application Support/Helios/SAR/runtime/bin/python3}" -m unittest discover -s "$helios_package/Tests/SAR"
