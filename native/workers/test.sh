#!/bin/bash
# Run the scientific Python worker tests.
set -euo pipefail
helios_native="$(cd "$(dirname "$0")/.." && pwd)"

if [[ -n "${HELIOS_SAR_PYTHON:-}" ]]; then
  helios_python="$HELIOS_SAR_PYTHON"
elif command -v python3 >/dev/null 2>&1; then
  helios_python="$(command -v python3)"
else
  echo "python3 was not found. Run make setup-raster or set HELIOS_SAR_PYTHON." >&2
  exit 1
fi

"$helios_python" -m unittest discover -s "$helios_native/workers/tests" "$@"
