#!/usr/bin/env bash
set -euo pipefail
helios_root="$(cd "$(dirname "$0")/../.." && pwd)"
helios_build="$helios_root/output/desktop/build"
helios_cesium="$helios_root/output/vendor/cesium-native"
if [[ ! -d "$helios_cesium/.git" ]]; then
  git clone --depth 1 --branch v0.65.0 https://github.com/CesiumGS/cesium-native.git "$helios_cesium"
fi
export EZVCPKG_BASEDIR="${EZVCPKG_BASEDIR:-$helios_root/output/vendor/vcpkg}"
export VCPKG_MAX_CONCURRENCY="${VCPKG_MAX_CONCURRENCY:-2}"
helios_args=(-S "$helios_root" -B "$helios_build" -DHELIOS_BUILD_DESKTOP=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo)
if [[ "$(uname -s)" == Darwin ]] && command -v brew >/dev/null 2>&1; then
  helios_qt="$(brew --prefix qtbase)"
  helios_shaders="$(brew --prefix qtshadertools)"
  helios_args+=("-DCMAKE_PREFIX_PATH=$helios_qt;$helios_shaders" "-DQT_ADDITIONAL_PACKAGES_PREFIX_PATH=$helios_shaders")
fi
cmake "${helios_args[@]}"
cmake --build "$helios_build" --parallel "${HELIOS_BUILD_JOBS:-2}"
if [[ "$(uname -s)" == Darwin ]]; then
  helios_app="$helios_build/bin/Helios.app"
  if [[ -n "${helios_qt:-}" ]]; then
    "$helios_qt/bin/macdeployqt" "$helios_app" -always-overwrite
  fi
  cp "$helios_root/LICENSE" "$helios_app/Contents/Resources/LICENSE"
  cp "$helios_root/NOTICE.md" "$helios_app/Contents/Resources/PROJECT-NOTICE.md"
  cp "$helios_root/native/desktop/NOTICE.md" "$helios_app/Contents/Resources/DESKTOP-NOTICE.md"
  cp "$helios_build/SCENE-NOTICES.txt" "$helios_app/Contents/Resources/SCENE-NOTICES.txt"
  python3 "$helios_root/native/desktop/packaging/collect-macos-notices.py" --app "$helios_app"
  codesign --force --deep --sign - "$helios_app"
  codesign --verify --deep --strict "$helios_app"
  printf 'Qt desktop preview: %s\n' "$helios_app"
else
  printf 'Qt desktop preview: %s\n' "$helios_build/bin/Helios"
fi
