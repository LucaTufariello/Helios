#!/bin/bash
set -euo pipefail
godeye_package="$(cd "$(dirname "$0")" && pwd)"
godeye_source="$godeye_package/.build/vendor/cesium-native"
godeye_commit="1bca8021711294cc83a5a032599ac72d9b3166c9"
if [[ "$(uname -m)" != "arm64" ]]; then echo "The scene engine targets Apple Silicon." >&2; exit 1; fi
command -v cmake >/dev/null
command -v pkg-config >/dev/null
if [[ ! -d "$godeye_source/.git" ]]; then
  git clone --depth 1 --branch v0.65.0 https://github.com/CesiumGS/cesium-native.git "$godeye_source"
fi
if [[ "$(git -C "$godeye_source" rev-parse HEAD)" != "$godeye_commit" ]]; then
  echo "Unexpected Cesium Native source revision; expected the pinned v0.65.0 commit." >&2; exit 1
fi
godeye_config_args=(-S "$godeye_package/engine" -B "$godeye_package/.build/scene")
if [[ ! -f "$godeye_package/.build/scene/.godeye-build-config" ]] || [[ "$(cat "$godeye_package/.build/scene/.godeye-build-config")" != "arm64-godeye-v1" ]]; then
  godeye_config_args+=(--fresh)
fi
cmake "${godeye_config_args[@]}" \
  -DGODEYE_CESIUM_SOURCE="$godeye_source" -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=14.0 \
  -DVCPKG_TARGET_TRIPLET=arm64-godeye -DVCPKG_OVERLAY_TRIPLETS="$godeye_package/engine/triplets"
echo "arm64-godeye-v1" > "$godeye_package/.build/scene/.godeye-build-config"
cmake --build "$godeye_package/.build/scene" --target GodEyeScene --parallel 4
python3 "$godeye_package/engine/package-libraries.py" "$godeye_package/.build/scene" "$godeye_source"
