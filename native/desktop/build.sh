#!/usr/bin/env bash
set -euo pipefail
godeye_root="$(cd "$(dirname "$0")/../.." && pwd)"
godeye_build="$godeye_root/output/desktop/build"
godeye_cesium="$godeye_root/output/vendor/cesium-native"
if [[ ! -d "$godeye_cesium/.git" ]]; then
  git clone --depth 1 --branch v0.65.0 https://github.com/CesiumGS/cesium-native.git "$godeye_cesium"
fi
export EZVCPKG_BASEDIR="${EZVCPKG_BASEDIR:-$godeye_root/output/vendor/vcpkg}"
export VCPKG_MAX_CONCURRENCY="${VCPKG_MAX_CONCURRENCY:-2}"
godeye_args=(-S "$godeye_root" -B "$godeye_build" -DGODEYE_BUILD_DESKTOP=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo)
if [[ "$(uname -s)" == Darwin ]] && command -v brew >/dev/null 2>&1; then
  godeye_qt="$(brew --prefix qtbase)"
  godeye_shaders="$(brew --prefix qtshadertools)"
  godeye_args+=("-DCMAKE_PREFIX_PATH=$godeye_qt;$godeye_shaders" "-DQT_ADDITIONAL_PACKAGES_PREFIX_PATH=$godeye_shaders")
fi
cmake "${godeye_args[@]}"
cmake --build "$godeye_build" --parallel "${GODEYE_BUILD_JOBS:-2}"
if [[ "$(uname -s)" == Darwin ]]; then
  godeye_app="$godeye_build/bin/GodEyeDesktop.app"
  if [[ -n "${godeye_qt:-}" ]]; then
    "$godeye_qt/bin/macdeployqt" "$godeye_app" -always-overwrite
  fi
  cp "$godeye_root/LICENSE" "$godeye_app/Contents/Resources/LICENSE"
  cp "$godeye_root/NOTICE.md" "$godeye_app/Contents/Resources/PROJECT-NOTICE.md"
  cp "$godeye_root/native/desktop/NOTICE.md" "$godeye_app/Contents/Resources/DESKTOP-NOTICE.md"
  cp "$godeye_build/SCENE-NOTICES.txt" "$godeye_app/Contents/Resources/SCENE-NOTICES.txt"
  python3 "$godeye_root/native/desktop/packaging/collect-macos-notices.py" --app "$godeye_app"
  codesign --force --deep --sign - "$godeye_app"
  codesign --verify --deep --strict "$godeye_app"
  printf 'Qt desktop preview: %s\n' "$godeye_app"
else
  printf 'Qt desktop preview: %s\n' "$godeye_build/bin/GodEyeDesktop"
fi
