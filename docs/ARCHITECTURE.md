# Application architecture

Helios is a compiled C++20/Qt 6 Widgets desktop application for Apple Silicon
macOS and Linux. The browser implementation and the earlier Swift macOS client
were retired; their code remains in Git history. Current capabilities and limits
are described in [CURRENT-STATE.md](CURRENT-STATE.md), with setup in the
[root README](../README.md).

## Boundaries

- `native/desktop/` owns the Qt windows, shared workspace model, storage, network
  clients, map tools, mission documents, satellite libraries and optional local
  assistance. `UI/` contains Widgets panels and windows; `Core/` contains validated
  models and services; `Graphics/` contains the GPU map and tile cache.
- `native/scene/` adapts the pinned Cesium Native engine to Qt RHI. Cesium selects
  and loads content; Helios prepares and renders GPU resources. Qt uses Metal on
  macOS and Vulkan on Linux behind the rendering boundary.
- `native/shared/` is a portable C++20 WGS84/geometry library with a plain C ABI
  and explicit success/failure results. It has no Qt, GPU or Apple dependency.
- `native/sgp4/` supplies compiled Vallado SGP4/SDP4 propagation. No Python or
  JavaScript runtime is used for live orbital rendering.
- `native/workers/` contains the scientific Python workers. They run as cancellable
  local processes behind the shared process/job boundary; SNAP GPT performs
  Sentinel-1 calibration and geocoding. Their outputs retain scientific values and
  processing provenance.
- Mission Control shares the map's model and satellite libraries. Persistent local
  documents and platform credentials remain independent of repository build files.

The Qt RHI private interfaces tie a binary to the matching Qt build; compile and
deploy the same Qt libraries and headers. Qt 6.8+ is required and Qt 6.11.2 is the
tested Linux version.

## Build and dependencies

Use `make test-desktop` (or `make build-desktop`) on Apple Silicon macOS or Linux.
The first build downloads the pinned Cesium Native v0.65.0 source and its
dependencies; later builds reuse `output/vendor/`. The macOS build deploys Qt
frameworks/plugins into an arm64 bundle and verifies a local ad-hoc signature. It is
not a notarized release.

The canonical reference datasets and transit registry live in `native/resources/`.
Their provenance and refresh guidance are in
[the resource maintenance notes](../native/resources/README.md).

Build outputs and downloaded native dependencies are ignored. The application
bundle contains its executable, the worker host and workers, resource data,
compiled shaders, credits and component notices.

## Terrain and photorealistic 3D Tiles

Open **3D Surface** in the native toolbar. Terrain is enabled by default and begins
loading below a 200 km camera range. The panel supplies tilt/heading sliders,
north-up, local `tileset.json` loading, HTTPS URL loading, coverage focus/removal
and visible source credits. Option/Alt-drag rotates/tilts the camera. Clicking the
surface inspects its coordinates, rendered height and the geometry source. A
tileset stays in the current session; its URL/local path is not exported or put in
view links.

### Real sources and implemented rendering

- **Keyless elevation:** [Mapzen terrain on AWS](https://registry.opendata.aws/terrain-tiles/),
  using the documented [Terrarium RGB encoding](https://github.com/tilezen/joerd/blob/master/docs/formats.md).
  Decoder dimensions are validated before decompression, and source channel values
  are read without color conversion. Fractional and negative values are retained;
  displayed ocean terrain is clamped to sea level. Full
  [source attribution](https://github.com/tilezen/joerd/blob/master/docs/attribution.md)
  is bundled in the panel and in [DATA_SOURCES.md](../DATA_SOURCES.md).
- **Actual photogrammetry:** the public AGI headquarters tileset listed in
  [Cesium's 3D Tiles resources](https://github.com/CesiumGS/3d-tiles/blob/main/RESOURCES.md)
  is available directly from the panel. It loads captured textured buildings and
  ground geometry. The app also accepts georeferenced local and same-origin HTTPS
  3D Tiles; it does not create mock buildings or simulated scenery. Public
  availability is not a blanket redistribution license.
- **Native engine:** [Cesium Native v0.65.0](https://github.com/CesiumGS/cesium-native/tree/v0.65.0),
  pinned to `1bca8021711294cc83a5a032599ac72d9b3166c9`, provides hierarchical
  selection, screen-space-error refinement and native glTF decoding. Helios supplies
  native background jobs, bounded network access, double-precision georeferencing
  and the RHI renderer. The app has no embedded web engine or service process.
- **Depth and precision:** an infinite-far reverse-Z projection uses one
  double-precision camera basis for rendering and picking. Terrain/mesh vertex
  buffers have local origins and receive camera-relative offsets. This avoids
  globe-coordinate precision loss in close views. Fine raster regions replace their
  parents in both depth rendering and picking; terrain boundary vertices conform to
  their loaded parent surface.
- **Draping and navigation:** raster imagery and NASA overlays use the terrain
  triangles. Surface routes, annotations and selected outlines follow the actual
  rendered terrain grid. Records with unknown altitude are positioned for display on
  the loaded ground; a reported altitude is not replaced or written back as
  inferred data. Camera range is relative to the loaded target surface, and
  clearance checks the geographically displaced eye at the current tilt/heading.
- **Picking:** terrain tests the exact uploaded raster triangles. 3D picking tests
  prepared mesh triangles, material cutouts/back faces and the subset actually
  uploaded to the GPU. An invisible/unprepared or memory-rejected mesh cannot be
  selected. Heights shown in the inspector describe the rendered geometry, rather
  than a provider measurement or an invented observation time.
- **Textures:** baked base-color photogrammetry, base-color factors, indexed
  triangles, triangle strip/fan conversion, UV sets, `KHR_texture_transform`,
  sampler wrap/filter modes, mipmaps, sRGB texture colors, opaque/masked/blended
  material handling and double-sided geometry are implemented. Geometry/content
  preparation remains asynchronous; unsupported or oversized content is counted as
  a failure.

### Bounds and current limits

Visible raster/terrain selection includes the tilted view footprint and the
displaced eye, with coarse coverage plus detail near the target/eye, at most 81
requested tile IDs per selection and a terrain zoom cap of 14. Elevation storage
holds at most 96 decoded tiles. Four elevation requests and four 3D tile requests
can run concurrently. 3D request bodies are capped at 32 MB; copied mesh/texture
preparation has a 64 MB per-content budget, the native tile cache targets 128 MB
and uploaded 3D GPU resources are capped at 256 MB. These are component budgets,
not a hard total process-memory guarantee. PNG/JPEG decoder dimensions are capped
at 8192 before allocation; other upstream compressed-content decoders still need
broader adversarial validation. Standard HTTP caching applies to terrain/raster
imagery; this is not a bulk/offline download tool.

The terrain model mixes source vertical datums and acquisition vintages. It has no
installed geoid correction and is not a survey-grade elevation service. Unknown
terrain remains an explicit ellipsoid fallback, including polar areas outside Web
Mercator coverage. Camera clearance applies to loaded geometry at the current view,
rather than guaranteeing clearance through unloaded terrain/buildings. Provider
mosaics can have visible color/resolution seams between imagery levels. Elevation-
edge conforming reduces mesh gaps but is not a complete multi-resolution terrain
geomorphing system.

The renderer currently targets georeferenced triangle photogrammetry. Full
physically based lighting, normal/metallic/roughness materials, point clouds,
splats, instanced tiles, structural feature metadata/semantic building identifiers,
and all optional 3D Tiles extensions are not complete. Alpha picking uses the base
texture level; a mip-filtered silhouette can differ at distant screen pixels.
Blended meshes are sorted by origin distance; intersecting translucent surfaces are
not fully order-independent. A custom dataset's correctness is not established
simply because it loads.

Local references are restricted to the chosen tileset directory/subdirectories
(including symlink resolution); network assets/redirects stay on the selected HTTPS
origin. Cross-origin texture/CDN datasets and authenticated ion/Google provider
flows are not included yet. No paid provider key is required for the implemented
terrain or public AGI coverage. Data/component licenses remain separate from the
project's MIT license. The generated app includes `SCENE-NOTICES.txt`; the build
downloads dependencies only into development caches, not the app bundle.

Development dependency builds may emit harmless empty-object archive warnings;
these do not replace build/signature validation. macOS 14 deployment flags are
used throughout, but runtime testing on a separate macOS 14 Mac remains
outstanding.

## Platform integration

- **Credentials:** `Core/Credentials.{h,cpp}` uses the macOS Keychain through
  `CredentialsMac.mm` and optional Linux Secret Service (libsecret). There is no
  plaintext fallback.
- **Camera playback:** native stills/clips use an owned FFmpeg process with
  argument arrays, bounded logs and cancellation. FFmpeg is bundled in the Linux
  artifact and external on macOS.
- **Local assistance:** optional CPU ONNX inference runs through `WorkerHost` with
  explicit model contracts. Apple Vision/Core ML is not used; the inference boundary
  is replaceable and no weights are downloaded automatically.
