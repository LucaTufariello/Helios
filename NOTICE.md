# Notices

Helios's source is distributed under the MIT license in [LICENSE](LICENSE).
The existing copyright notice for Bilawal Sidhu is retained. The project began
under an earlier name; its former browser implementation is preserved in Git history.
The maintained application is now the native macOS code under `native/macos/`.

Native components and datasets have separate licenses:

- Cesium Native and its transitive libraries: pinned by the native CMake build;
  the generated application includes `SCENE-NOTICES.txt`.
- Vallado SGP4 C++: source revision and modifications in
  `native/macos/Sources/CSGP4/UPSTREAM.md`; MIT notice in
  `native/macos/Sources/HeliosCore/Resources/SGP4-LICENSE.txt`.
- OpenStreetMap-derived datacenter/dam extracts: ODbL 1.0 with retained source
  attribution and resource-specific READMEs.
- TeleGeography cable systems and landing points: CC BY-NC-SA 3.0, including
  non-commercial and share-alike restrictions. These data are not covered by MIT.
- Terrain, basemap, imagery, live providers, SNAP, Python packages and optional
  detector models: their own terms and notices apply.

See [DATA_SOURCES.md](DATA_SOURCES.md) and the bundled resource READMEs. Removal
of the website does not remove or supersede any retained third-party notice.
