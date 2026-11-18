#!/bin/bash
set -euo pipefail
helios_package="$(cd "$(dirname "$0")" && pwd)"
helios_root="$(cd "$helios_package/../.." && pwd)"
if [[ "$(uname -m)" != "arm64" ]]; then
  echo "This build targets Apple Silicon. Run on an arm64 Mac." >&2
  exit 1
fi
"$helios_package/build-engine.sh"
swift build --package-path "$helios_package" --scratch-path "$helios_package/.build" -c release --arch arm64
helios_products="$(swift build --package-path "$helios_package" --scratch-path "$helios_package/.build" -c release --arch arm64 --show-bin-path)"
helios_app="$helios_root/output/native/Helios.app"
mkdir -p "$helios_app/Contents/MacOS" "$helios_app/Contents/Resources"
cp "$helios_products/HeliosMac" "$helios_app/Contents/MacOS/HeliosMac"
cp "$helios_package/Info.plist" "$helios_app/Contents/Info.plist"
ditto "$helios_products/HeliosNative_HeliosMac.bundle" "$helios_app/Contents/Resources/HeliosNative_HeliosMac.bundle"
# Replace the core resource bundle so old generated catalogs cannot survive a rebuild.
rm -rf "$helios_app/Contents/Resources/HeliosNative_HeliosCore.bundle"
ditto "$helios_products/HeliosNative_HeliosCore.bundle" "$helios_app/Contents/Resources/HeliosNative_HeliosCore.bundle"
# SwiftPM's cached directory copy may retain removed files from an earlier build.
helios_core_bundle="$helios_app/Contents/Resources/HeliosNative_HeliosCore.bundle"
if [[ -d "$helios_core_bundle/Contents/Resources" ]]; then helios_core_bundle="$helios_core_bundle/Contents/Resources"; fi
rm -rf "$helios_core_bundle/Resources/neighborhoods"
cp "$helios_root/LICENSE" "$helios_app/Contents/Resources/LICENSE"
cp "$helios_package/.build/scene/SCENE-NOTICES.txt" "$helios_app/Contents/Resources/SCENE-NOTICES.txt"
helios_work="$(mktemp -d "${TMPDIR:-/tmp}/helios-native.XXXXXX")"
trap 'rm -rf "$helios_work"' EXIT
xcrun -sdk macosx metal -mmacosx-version-min=14.0 -c "$helios_package/Sources/HeliosMac/Resources/Globe.metal" -o "$helios_work/Globe.air"
helios_bundle="$helios_app/Contents/Resources/HeliosNative_HeliosMac.bundle"
if [[ -d "$helios_bundle/Contents/Resources" ]]; then helios_bundle="$helios_bundle/Contents/Resources"; fi
mkdir -p "$helios_bundle/Resources"
xcrun -sdk macosx metallib "$helios_work/Globe.air" -o "$helios_bundle/Resources/Globe.metallib"
swift "$helios_package/Icon.swift" "$helios_work/AppIcon.iconset"
iconutil -c icns "$helios_work/AppIcon.iconset" -o "$helios_app/Contents/Resources/AppIcon.icns"
# Local ad-hoc signature, without disabling Gatekeeper or changing system settings.
codesign --force --sign - "$helios_app"
codesign --verify --strict "$helios_app"
file "$helios_app/Contents/MacOS/HeliosMac"
echo "Native app: $helios_app"
