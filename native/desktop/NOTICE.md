# Desktop preview notices

GodEye's new desktop source follows the repository MIT license and preserves the
existing project author notices. The Qt client is compiled C++/Qt Widgets; it
does not bundle WebEngine, Qt Quick/QML or an Electron runtime.

Qt Core, GUI, Widgets, Network, Concurrent and Test are dynamically linked Qt
components with their own open-source licensing terms. The build-time Qt Shader
Tools produce the portable GPU shader resources. The preview was built/tested
with Qt 6.11.2; Qt's private RHI interface ties deployed binaries to the exact Qt
build used to compile them. Rebuild from source against the Linux installation.

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
