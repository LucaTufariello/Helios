# Native macOS architecture

Helios is a native Apple Silicon macOS application. The browser implementation
has been retired; historical code remains in Git. Current capabilities and limits
are described in [CURRENT-STATE.md](CURRENT-STATE.md), with setup in the
[root README](../README.md).

The [cross-platform transition](CROSS-PLATFORM.md) now targets macOS and Linux.
The current client remains SwiftUI/AppKit/Metal. Its WGS84 conversion, camera
basis and ray-picking calculations use the shared `native/shared` C++20 library,
linked through a C ABI by SwiftPM; the same source builds independently with CMake.

## Application boundaries

- `HeliosMac` owns SwiftUI/AppKit windows, shared observable workspace state,
  native media playback, platform storage integration and Metal rendering.
- `HeliosCore` owns validated documents, provider transports/parsers, orbital and
  geographic models, canonical reference resources and scientific-worker orchestration.
- `CScene` and the Objective-C++ engine bridge connect the Swift renderer to pinned
  Cesium Native. Cesium selects/loads content; Helios prepares and renders GPU resources.
- `CSGP4` supplies compiled Vallado SGP4/SDP4 propagation. No Python/JavaScript is
  used for live orbital rendering.
- Optional satellite Python workers run as cancellable local processes behind
  `SarProcessor`; SNAP GPT performs Sentinel-1 calibration/geocoding. Their UI is native.
- Mission Control shares the map's model and satellite libraries. Persistent local
  documents and Keychain credentials remain independent of repository build files.

## Build and dependencies

Use `make build`, `make test`, `make test-raster` and `make format-check` on an
arm64 Mac with full Xcode, CMake 3.24+, Python 3 and pkg-config. See [TESTING.md](../TESTING.md)
for opt-in provider/window checks. No npm command or JavaScript packaging step is needed.

SwiftPM packages resources directly from `Sources/HeliosCore/Resources/`; these are
the canonical reference datasets and transit registry. Frozen independent orbital
vectors remain in the test fixtures. Their provenance and refresh guidance are in
[resource maintenance](../native/macos/scripts/README.md).

Build outputs and downloaded native dependencies are ignored. The app bundle contains
its release executable, Swift resources, compiled Metal shaders, icon, source license
and component notices. It is locally signed, not notarized.

## Terrain and photorealistic 3D Tiles

Open **3D Surface** in the native toolbar. Terrain is enabled by default and begins loading below a 200 km camera range. The panel supplies tilt/heading sliders, north-up, local `tileset.json` loading, HTTPS URL loading, coverage focus/removal and visible source credits. Option-drag rotates/tilts the camera. Clicking the surface inspects its coordinates, rendered height and the geometry source. A tileset stays in the current session; its URL/local path is not exported or put in view links.

### Real sources and implemented rendering

- **Keyless elevation:** [Mapzen terrain on AWS](https://registry.opendata.aws/terrain-tiles/), using the documented [Terrarium RGB encoding](https://github.com/tilezen/joerd/blob/master/docs/formats.md). ImageIO validates 256×256 images before decompression, and source channel values are read without color conversion. Fractional and negative values are retained; displayed ocean terrain is clamped to sea level. Full [source attribution](https://github.com/tilezen/joerd/blob/master/docs/attribution.md) is bundled and accessible in the native panel.
- **Actual photogrammetry:** the public AGI headquarters tileset listed in [Cesium's 3D Tiles resources](https://github.com/CesiumGS/3d-tiles/blob/main/RESOURCES.md) is available directly from the panel. It loads captured textured buildings and ground geometry. The app also accepts georeferenced local and same-origin HTTPS 3D Tiles; it does not create mock buildings or simulated scenery. Public availability is not a blanket redistribution license.
- **Native engine:** [Cesium Native v0.65.0](https://github.com/CesiumGS/cesium-native/tree/v0.65.0), pinned to `1bca8021711294cc83a5a032599ac72d9b3166c9`, provides hierarchical selection, screen-space-error refinement and native glTF decoding. Helios supplies native background jobs, bounded NSURLSession access, double-precision georeferencing and the Metal renderer. The app has no embedded web engine or service process.
- **Depth and precision:** an infinite-far reverse-Z projection uses one double-precision camera basis for rendering and picking. Terrain/mesh vertex buffers have local origins and receive camera-relative offsets. This avoids globe-coordinate precision loss in close views. Fine raster regions replace their parents in both depth rendering and picking; terrain boundary vertices conform to their loaded parent surface.
- **Draping and navigation:** raster imagery and NASA overlays use the terrain triangles. Surface routes, annotations and selected outlines follow the actual rendered terrain grid. Records with unknown altitude are positioned for display on the loaded ground; a reported altitude is not replaced or written back as inferred data. Camera range is relative to the loaded target surface, and clearance checks the geographically displaced eye at the current tilt/heading. Following and route navigation use the same clearance check.
- **Picking:** terrain tests the exact uploaded raster triangles. 3D picking tests prepared mesh triangles, material cutouts/back faces and the subset actually uploaded to the GPU. An invisible/unprepared or memory-rejected mesh cannot be selected. Heights shown in the inspector describe the rendered geometry, rather than a provider measurement or an invented observation time.
- **Textures:** baked base-color photogrammetry, base-color factors, indexed triangles, triangle strip/fan conversion, UV sets, `KHR_texture_transform`, sampler wrap/filter modes, mipmaps, sRGB texture colors, opaque/masked/blended material handling and double-sided geometry are implemented. Geometry/content preparation remains asynchronous; unsupported or oversized content is counted as a failure.

### Bounds and current limits

Visible raster/terrain selection includes the tilted view footprint and the displaced eye, with coarse coverage plus detail near the target/eye, at most 81 requested tile IDs per selection and a terrain zoom cap of 14. Elevation storage holds at most 96 decoded tiles. Four elevation requests and four 3D tile requests can run concurrently. 3D request bodies are capped at 32 MB; copied mesh/texture preparation has a 64 MB per-content budget, the native tile cache targets 128 MB and uploaded 3D GPU resources are capped at 256 MB. These are component budgets, not a hard total process-memory guarantee. PNG/JPEG decoder dimensions are capped at 8192 before allocation; other upstream compressed-content decoders still need broader adversarial validation. Standard HTTP caching applies to terrain/raster imagery; this is not a bulk/offline download tool.

The terrain model mixes source vertical datums and acquisition vintages. It has no installed geoid correction and is not a survey-grade elevation service. Unknown terrain remains an explicit ellipsoid fallback, including polar areas outside Web Mercator coverage. Camera clearance applies to loaded geometry at the current view, rather than guaranteeing clearance through unloaded terrain/buildings. Provider mosaics can have visible color/resolution seams between imagery levels. Elevation-edge conforming reduces mesh gaps but is not a complete multi-resolution terrain geomorphing system.

The renderer currently targets georeferenced triangle photogrammetry. Full physically based lighting, normal/metallic/roughness materials, point clouds, splats, instanced tiles, structural feature metadata/semantic building identifiers, and all optional 3D Tiles extensions are not complete. Alpha picking uses the base texture level; a mip-filtered silhouette can differ at distant screen pixels. Blended meshes are sorted by origin distance; intersecting translucent surfaces are not fully order-independent. A custom dataset's correctness is not established simply because it loads.

Local references are restricted to the chosen tileset directory/subdirectories (including symlink resolution); network assets/redirects stay on the selected HTTPS origin. Cross-origin texture/CDN datasets and authenticated ion/Google provider flows are not included yet. No paid provider key is required for the implemented terrain or public AGI coverage. Data/component licenses remain separate from the project's MIT license. The generated app includes `SCENE-NOTICES.txt`; the build downloads dependencies only into development caches, not the app bundle.

The first engine build needs network access and can take several minutes. `build-engine.sh` uses an arm64/macOS 14 dependency triplet, pins source/dependency revisions and reuses subsequent builds. A generated archive fingerprint ensures SwiftPM relinks when the external C++ engine changes. Development dependency builds may emit harmless empty-object archive warnings; these do not replace build/signature validation. macOS 14 deployment flags are used throughout, but runtime testing on a separate macOS 14 Mac remains outstanding.
