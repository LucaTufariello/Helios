#!/bin/bash
set -euo pipefail
godeye_package="$(cd "$(dirname "$0")" && pwd)"
godeye_root="$(cd "$godeye_package/../.." && pwd)"
if [[ "$(uname -m)" != "arm64" ]]; then
  echo "This build targets Apple Silicon. Run on an arm64 Mac." >&2
  exit 1
fi
"$godeye_package/build-engine.sh"
swift build --package-path "$godeye_package" --scratch-path "$godeye_package/.build" -c release --arch arm64
godeye_products="$(swift build --package-path "$godeye_package" --scratch-path "$godeye_package/.build" -c release --arch arm64 --show-bin-path)"
godeye_app="$godeye_root/output/native/GodEye.app"
mkdir -p "$godeye_app/Contents/MacOS" "$godeye_app/Contents/Resources"
cp "$godeye_products/GodEyeMac" "$godeye_app/Contents/MacOS/GodEyeMac"
cp "$godeye_package/Info.plist" "$godeye_app/Contents/Info.plist"
ditto "$godeye_products/GodEyeNative_GodEyeMac.bundle" "$godeye_app/Contents/Resources/GodEyeNative_GodEyeMac.bundle"
# Replace the core resource bundle so old generated catalogs cannot survive a rebuild.
rm -rf "$godeye_app/Contents/Resources/GodEyeNative_GodEyeCore.bundle"
ditto "$godeye_products/GodEyeNative_GodEyeCore.bundle" "$godeye_app/Contents/Resources/GodEyeNative_GodEyeCore.bundle"
# SwiftPM's cached directory copy may retain removed files from an earlier build.
godeye_core_bundle="$godeye_app/Contents/Resources/GodEyeNative_GodEyeCore.bundle"
if [[ -d "$godeye_core_bundle/Contents/Resources" ]]; then godeye_core_bundle="$godeye_core_bundle/Contents/Resources"; fi
rm -rf "$godeye_core_bundle/Resources/neighborhoods"
cp "$godeye_root/LICENSE" "$godeye_app/Contents/Resources/LICENSE"
cp "$godeye_package/.build/scene/SCENE-NOTICES.txt" "$godeye_app/Contents/Resources/SCENE-NOTICES.txt"
godeye_work="$(mktemp -d "${TMPDIR:-/tmp}/godeye-native.XXXXXX")"
trap 'rm -rf "$godeye_work"' EXIT
xcrun -sdk macosx metal -mmacosx-version-min=14.0 -c "$godeye_package/Sources/GodEyeMac/Resources/Globe.metal" -o "$godeye_work/Globe.air"
godeye_bundle="$godeye_app/Contents/Resources/GodEyeNative_GodEyeMac.bundle"
if [[ -d "$godeye_bundle/Contents/Resources" ]]; then godeye_bundle="$godeye_bundle/Contents/Resources"; fi
mkdir -p "$godeye_bundle/Resources"
xcrun -sdk macosx metallib "$godeye_work/Globe.air" -o "$godeye_bundle/Resources/Globe.metallib"
swift "$godeye_package/Icon.swift" "$godeye_work/AppIcon.iconset"
iconutil -c icns "$godeye_work/AppIcon.iconset" -o "$godeye_app/Contents/Resources/AppIcon.icns"
# Local ad-hoc signature, without disabling Gatekeeper or changing system settings.
codesign --force --sign - "$godeye_app"
codesign --verify --strict "$godeye_app"
file "$godeye_app/Contents/MacOS/GodEyeMac"
echo "Native app: $godeye_app"
