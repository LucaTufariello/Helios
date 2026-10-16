# Desktop preview notices

GodEye's new desktop source follows the repository MIT license and preserves the
existing project author notices. The Qt client is compiled C++/Qt Widgets; it
does not bundle WebEngine, Qt Quick/QML or an Electron runtime.

Qt Core, GUI, Widgets, Network, Concurrent and Test are dynamically linked Qt
components with their own open-source licensing terms. The build-time Qt Shader
Tools produce the portable GPU shader resources. The preview was built/tested
with Qt 6.11.2; Qt's private RHI interface ties deployed binaries to the exact Qt
build used to compile them. Rebuild from source against the Linux installation.

The desktop links Cesium Native v0.65.0 at the existing pinned revision. Its
Apache-2.0 license and the notices accompanying the actual static dependencies
are collected in `SCENE-NOTICES.txt` during configuration and installed with the
application. Public scene data retains its own attribution, displayed by the map.

Linux camera clip playback uses a separate FFmpeg process. The Linux package
includes the distribution FFmpeg executable and libav/libsw shared libraries,
their license texts and build configuration. This build enables GPL components
and reports GPL version 3 or later. Its matching upstream source plus Arch recipe
and patch archive accompany the application artifact. Other codec libraries are
explicit host dependencies. FFmpeg is not linked into GodEye's executable; its
frames are displayed by Qt Widgets. No Qt Quick/QML or browser media interface
is used. On macOS, FFmpeg is an explicit external clip-playback dependency.

Consult [Qt licensing](https://doc.qt.io/qt-6/licensing.html), the licenses shipped
with the selected Qt distribution, and the dependency inventory before publishing
a distributable release. Ad-hoc signing of a local macOS preview is not notarization.

Basemap tiles and terrain remain provider data, with attribution in the map and
provider terms independent of the source-code license. Satellite manifests retain
their source and processing metadata; the client displays existing previews and
does not independently certify calibration or geographic accuracy.

The validation harness uses explicitly labeled generated QA imagery/documents.
Opt-in live checks separately use actual public map/terrain data or a user-selected
processed product. Test fixtures are never inserted into the normal application
profile.
