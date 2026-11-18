#!/bin/bash
set -euo pipefail
helios_package="$(cd "$(dirname "$0")" && pwd)"
helios_source="$helios_package/.build/vendor/cesium-native"
helios_commit="1bca8021711294cc83a5a032599ac72d9b3166c9"
if [[ "$(uname -m)" != "arm64" ]]; then echo "The scene engine targets Apple Silicon." >&2; exit 1; fi
command -v cmake >/dev/null
command -v pkg-config >/dev/null
if [[ ! -d "$helios_source/.git" ]]; then
  git clone --depth 1 --branch v0.65.0 https://github.com/CesiumGS/cesium-native.git "$helios_source"
fi
if [[ "$(git -C "$helios_source" rev-parse HEAD)" != "$helios_commit" ]]; then
  echo "Unexpected Cesium Native source revision; expected the pinned v0.65.0 commit." >&2; exit 1
fi
helios_config_args=(-S "$helios_package/engine" -B "$helios_package/.build/scene")
if [[ ! -f "$helios_package/.build/scene/.helios-build-config" ]] || [[ "$(cat "$helios_package/.build/scene/.helios-build-config")" != "arm64-helios-v1" ]]; then
  helios_config_args+=(--fresh)
fi
cmake "${helios_config_args[@]}" \
  -DHELIOS_CESIUM_SOURCE="$helios_source" -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=14.0 \
  -DVCPKG_TARGET_TRIPLET=arm64-helios -DVCPKG_OVERLAY_TRIPLETS="$helios_package/engine/triplets"
echo "arm64-helios-v1" > "$helios_package/.build/scene/.helios-build-config"
cmake --build "$helios_package/.build/scene" --target HeliosScene --parallel 4
python3 "$helios_package/engine/package-libraries.py" "$helios_package/.build/scene" "$helios_source"
