#!/bin/bash
# Check or apply clang-format to the hand-written C++ sources.
set -euo pipefail
helios_root="$(cd "$(dirname "$0")/.." && pwd)"

if [[ -n "${CLANG_FORMAT:-}" ]]; then
  helios_clang_format="$CLANG_FORMAT"
elif command -v clang-format >/dev/null 2>&1; then
  helios_clang_format="$(command -v clang-format)"
elif command -v xcrun >/dev/null 2>&1 && xcrun -f clang-format >/dev/null 2>&1; then
  helios_clang_format="$(xcrun -f clang-format)"
else
  echo "clang-format was not found. Install it or set CLANG_FORMAT." >&2
  exit 1
fi

case "${1:---check}" in
  --check) helios_format_args=(--dry-run --Werror) ;;
  --write) helios_format_args=(-i) ;;
  *)
    echo "Usage: $0 [--check|--write]" >&2
    exit 2
    ;;
esac

# Upstream SGP4 sources keep their original formatting; only the Helios wrapper
# and the application/shared/scene code are formatted.
helios_files=()
while IFS= read -r helios_file; do
  helios_files+=("$helios_file")
done < <(
  find "$helios_root/native/desktop" "$helios_root/native/shared" \
    "$helios_root/native/scene" "$helios_root/native/sgp4" \
    \( -name '*.h' -o -name '*.cpp' -o -name '*.mm' -o -name '*.c' \) \
    -not -path '*/build/*' -not -name 'SGP4.cpp' -not -name 'SGP4.h' -print |
    sort
)

if [[ ${#helios_files[@]} -eq 0 ]]; then
  echo "No C++ sources found." >&2
  exit 1
fi

"$helios_clang_format" "${helios_format_args[@]}" "${helios_files[@]}"
