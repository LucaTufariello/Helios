#!/bin/bash
set -euo pipefail
godeye_package="$(cd "$(dirname "$0")" && pwd)"
"$godeye_package/build-engine.sh"
swift test --package-path "$godeye_package" --arch arm64 "$@"
