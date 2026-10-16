# Native map tools core

`Core/MapTools.{h,cpp}` ports the existing Photon lookup, public OSRM directions,
local annotation documents, Swift version 1 workspaces/share links and approximate
solar terminator. The native UI and map renderer consume this core; the network
and document code has no graphics dependency.

`MapLookup` exposes `search(query)`, `route(start,end,mode)`, `cancel()`, `busy()`,
`error()`, `places()` and `result()`, with `changed` and `completed(bool)` signals.
Create separate instances for independent place search and directions. HTTPS URLs
are fixed to the existing public Photon and routing.openstreetmap.de providers.
Queries are sent only on explicit user actions. A place query is 1–200 characters,
its response is capped at 1 MiB and at most six validated named points are retained.
Car, foot and bike routes require endpoints within a 1,000 km ECEF chord; responses
are capped at 8 MiB, 30,000 points and 2,000 instructions. Distance and duration are
finite and bounded. Provider data is parsed off the UI thread with one decode per
instance. Cancellation invalidates the generation, aborts the owned HTTP request
and waits only for an already-running bounded parse to finish before another
request can start. A failed request retains the prior admitted result. HTTP
redirects and cookie storage are disabled by the existing bounded `HttpRequest`.

The public services have availability/usage limits. Photon documents its
[demo service and API](https://github.com/komoot/photon); route geometry and
instructions follow the [OSRM route contract](https://project-osrm.org/docs/v5.24.0/api/).
Use Photon/OpenStreetMap and OSRM/OpenStreetMap attribution in the UI. A route is
a provider estimate, not observed movement or a navigation guarantee. Directions
do not establish access rights, road conditions or reliable arrival times.

`AnnotationLibrary` explicitly loads an array from a user document, then offers
atomic `replace/add/update/remove` through `JsonFile`. Limits match the Swift
schema: at most 500 unique UUIDs, nonempty titles up to 256 UTF-16 code units,
1–2,000 valid geographic points, at least three points for a closed shape and a
finite Foundation-epoch `createdAt`. Unknown entry fields survive edits to known
fields. The 2 MiB file cap, last-good backup, conflict detection, lock and atomic
publication protect existing bytes. A corrupt library disables replacement until
the user recovers it. Removing an annotation does not cascade into mission evidence
snapshots. An annotation is user-authored geometry, not a provider measurement.

`validatedWorkspace` checks known Swift v1 fields and retains unknown fields for
local JSON I/O. It validates camera, layers/providers, display choices, opacity,
annotations and tracked identifier; old removed `neighborhoods` layers/tracking
are discarded, and absent legacy terrain defaults to true. If an active daily
imagery date is present it must be a canonical UTC date from 2000 through today,
before any UI/library mutation. `loadWorkspace/saveWorkspace` perform bounded
validated import/export without replacing corrupt existing documents.

`cameraShareURL/cameraFromShareURL` retain `godeye://view` latitude, longitude,
altitude, heading and tilt. `workspaceShareURL/workspaceFromShareURL` retain Swift's
base64 JSON `godeye://workspace?state=…` contract. Links reject duplicate or malformed
fields and size overruns. Shared workspaces exclude annotations and unknown root
or camera fields; provider credentials are never read or inserted. Incoming shared
links with annotation content are rejected. Imported local unknown fields stay
local, so preserving future schema fields cannot silently disclose them in links.

`SolarState` reproduces the Swift approximate seasonal declination and UTC
subsolar longitude, with solar elevation and a 721-point orthogonal great-circle
terminator. It is an approximate day/night display, not an astronomical ephemeris,
measured illumination or acquisition-time correction.

`Tests/MapToolsTests.cpp` exercises URL/query bounds, invalid/oversized JSON,
route geometry/instructions, cancellation during network and parsing, retained
results, annotation conflict/corrupt recovery/unknown-field round trips, workspace
privacy/legacy fields/date admission, duplicate share fields and solar geometry.
The default suite passes nine checks and skips two opt-in public service checks:

```sh
GODEYE_MAPTOOLS_LIVE=1 GODEYE_MAPTOOLS_QA=/absolute/path/to/qa \
  ./output/desktop/build/native/desktop/GodEyeMapToolsTests
```

On 2026-10-15, the actual walking route between (45.4641,9.1919) and
(45.4700,9.2000) passed through the public OSRM endpoint. The admitted geometry,
distance, duration and instruction counts are recorded in
`output/qa/linux-migration-20261015/map-tools/osrm-live.json`. The actual Photon
request for `Milano Italia` timed out without a response in the Qt check; an
independent curl check also received no bytes before its deadline. That public
Photon acceptance gate remains unverified; fixture/parser/cancellation tests pass.
No guessed place result or automatic alternate provider was substituted.
