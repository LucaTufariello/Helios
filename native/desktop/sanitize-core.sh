#!/usr/bin/env bash
set -euo pipefail
helios_root="$(cd "$(dirname "$0")/../.." && pwd)"
helios_build="${HELIOS_SANITIZER_BUILD:-$helios_root/output/desktop/sanitizers}"
helios_args=(-S "$helios_root/native/desktop/sanitizers" -B "$helios_build" -DCMAKE_BUILD_TYPE=Debug)
export EZVCPKG_BASEDIR="${EZVCPKG_BASEDIR:-$helios_root/output/vendor/vcpkg}"
export VCPKG_MAX_CONCURRENCY="${VCPKG_MAX_CONCURRENCY:-2}"
if [[ "$(uname -s)" == Darwin ]] && command -v brew >/dev/null 2>&1; then
  helios_args+=("-DCMAKE_PREFIX_PATH=$(brew --prefix qtbase);$(brew --prefix qtshadertools)"
    "-DQT_ADDITIONAL_PACKAGES_PREFIX_PATH=$(brew --prefix qtshadertools)")
fi
cmake "${helios_args[@]}"
helios_targets=()
while IFS= read -r helios_target; do
  helios_targets+=("$helios_target")
done < "$helios_build/sanitizer-targets.txt"
if [[ "${#helios_targets[@]}" == 0 ]]; then
  printf 'No owned sanitizer test targets were configured.\n' >&2
  exit 1
fi
cmake --build "$helios_build" --parallel "${HELIOS_BUILD_JOBS:-2}" --target "${helios_targets[@]}"
# Qt/system libraries are external. Leak detection is a separate gate; report
# this setting when citing results. This run checks owned CPU code, not GPU or
# upstream scene allocation behavior.
export ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0:abort_on_error=1}"
export UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1:print_stacktrace=1}"
ctest --test-dir "$helios_build" --exclude-regex '^desktop-scene$' --output-on-failure
