#!/bin/bash
set -euo pipefail
helios_package="$(cd "$(dirname "$0")" && pwd)"
"$helios_package/build-engine.sh"
swift test --package-path "$helios_package" --arch arm64 "$@"
