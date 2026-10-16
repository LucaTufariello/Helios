#!/usr/bin/env bash
set -euo pipefail
godeye_root="$(cd "$(dirname "$0")/../.." && pwd)"
godeye_build="${GODEYE_SANITIZER_BUILD:-$godeye_root/output/desktop/sanitizers}"
godeye_args=(-S "$godeye_root/native/desktop/sanitizers" -B "$godeye_build" -DCMAKE_BUILD_TYPE=Debug)
export EZVCPKG_BASEDIR="${EZVCPKG_BASEDIR:-$godeye_root/output/vendor/vcpkg}"
export VCPKG_MAX_CONCURRENCY="${VCPKG_MAX_CONCURRENCY:-2}"
if [[ "$(uname -s)" == Darwin ]] && command -v brew >/dev/null 2>&1; then
  godeye_args+=("-DCMAKE_PREFIX_PATH=$(brew --prefix qtbase);$(brew --prefix qtshadertools)"
    "-DQT_ADDITIONAL_PACKAGES_PREFIX_PATH=$(brew --prefix qtshadertools)")
fi
cmake "${godeye_args[@]}"
godeye_targets=()
while IFS= read -r godeye_target; do
  godeye_targets+=("$godeye_target")
done < "$godeye_build/sanitizer-targets.txt"
if [[ "${#godeye_targets[@]}" == 0 ]]; then
  printf 'No owned sanitizer test targets were configured.\n' >&2
  exit 1
fi
cmake --build "$godeye_build" --parallel "${GODEYE_BUILD_JOBS:-2}" --target "${godeye_targets[@]}"
# Qt/system libraries are external. Leak detection is a separate gate; report
# this setting when citing results. This run checks owned CPU code, not GPU or
# upstream scene allocation behavior.
export ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0:abort_on_error=1}"
export UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1:print_stacktrace=1}"
ctest --test-dir "$godeye_build" --exclude-regex '^desktop-scene$' --output-on-failure
