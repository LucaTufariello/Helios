#!/bin/bash
set -euo pipefail
godeye_package="$(cd "$(dirname "$0")" && pwd)"
case "${1:---check}" in
  --check) godeye_format_args=(lint --strict --recursive) ;;
  --write) godeye_format_args=(format --in-place --recursive) ;;
  *) echo "Usage: $0 [--check|--write]" >&2; exit 2 ;;
esac
xcrun swift-format "${godeye_format_args[@]}" \
  "$godeye_package/Sources" "$godeye_package/Tests" \
  "$godeye_package/Package.swift" "$godeye_package/Icon.swift" \
  "$godeye_package/../shared/Package.swift"
