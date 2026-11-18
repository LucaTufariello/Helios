# Security

Helios is a local native macOS app. It reads public network sources, imported
geographic/image files and mission documents, and optionally invokes local
Python raster workers and ESA SNAP GPT.

## Reporting

Report vulnerabilities privately through this repository's GitHub security
advisories when available. If private reporting is unavailable, contact the
repository owner privately before sharing exploit details. Do not open a public
issue containing credentials, private mission data or a working exploit. Include
the affected commit, macOS/architecture, reproduction and a minimal sanitized file
or diagnostic excerpt.

## Boundaries to preserve

- Store provider credentials in macOS Keychain. Mission exports, view links,
  logs and fixtures must not contain keys or passwords.
- Treat SAFE/ZIP/XML, HDF/raster, GeoJSON, 3D Tiles and imported mission documents
  as untrusted. Preserve byte/dimension/entry limits, path and symlink checks,
  XML entity restrictions, validated schemas and nodata handling.
- Spawn external tools with executable URLs and argument arrays. Do not interpolate
  source paths or user input into shell commands. Validate selected executables.
- Keep remote requests bounded and cancellable. Scene resources and redirects are
  restricted to the selected HTTPS origin; local scene references stay beneath
  the selected directory after symlink resolution.
- Keep scientific processing off the main thread and use isolated job directories,
  bounded developer diagnostics and cleanup on failure/cancellation.
- Keep native dependency revisions pinned and retain component/data licenses.

The build currently produces a local ad-hoc signed app, not a notarized release.
Network providers and separately installed SNAP/Python/model dependencies have
independent security/update responsibilities. Broader adversarial decoder testing
remains needed; successful loading does not establish a file's safety or accuracy.
