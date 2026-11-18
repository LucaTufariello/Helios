#!/bin/bash
set -euo pipefail
helios_package="$(cd "$(dirname "$0")" && pwd)"
case "${1:---check}" in
  --check) helios_format_args=(lint --strict --recursive) ;;
  --write) helios_format_args=(format --in-place --recursive) ;;
  *) echo "Usage: $0 [--check|--write]" >&2; exit 2 ;;
esac
xcrun swift-format "${helios_format_args[@]}" \
  "$helios_package/Sources" "$helios_package/Tests" \
  "$helios_package/Package.swift" "$helios_package/Icon.swift" \
  "$helios_package/../shared/Package.swift"
