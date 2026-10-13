#!/usr/bin/env bash
set -euo pipefail
godeye_root="$(cd "$(dirname "$0")/../.." && pwd)"
godeye_build="$godeye_root/output/desktop/build"
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
  codesign --force --deep --sign - "$godeye_app"
  codesign --verify --deep --strict "$godeye_app"
  printf 'Qt desktop preview: %s\n' "$godeye_app"
else
  printf 'Qt desktop preview: %s\n' "$godeye_build/bin/GodEyeDesktop"
fi
