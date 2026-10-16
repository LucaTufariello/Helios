# Native domain sources

The Qt Sources & observations window ports the 19 enabled Swift `FeedKind` values.
It uses compiled Widgets, bounded native HTTPS and a fixed-endpoint TLS WebSocket
client. Source records, stored mission snapshots, retained observed fixes and
propagated orbital estimates remain distinct. Local annotations are supplied by
the map editor; this window does not create a second annotation library.

| Section | Sources |
| --- | --- |
| Marine | AISStream vessel reports; TeleGeography cable systems and landing points |
| Sky | OpenSky flights with regional adsb.lol fallback; adsb.lol military reports; CelesTrak satellites; Launch Library 2; Open-Meteo weather and CAMS air-quality models |
| Ground | OSM mapped installations and energy/refining; bundled datacenters and dams; seven public GTFS-realtime operators; TomTom road flow; TfL/Fintraffic cameras; USGS earthquakes; GDACS alerts; NASA FIRMS thermal detections; local annotations |
| Satellite | Copernicus product metadata and footprints; actual optical/SAR processing uses the separate processing center |

Bike share, radio, cockpit, voice and neighborhood boundaries remain excluded.

## Setup and operation

Enable a source, select a record, inspect its details/raw provider fields, and use
Show on map, Estimated orbit, Add evidence or Export record. Provider region
actions focus the actual selected camera/transit provider's coverage area.
Filtering sources or selecting a workspace section does not delete source data.

Provider settings saves optional `firms`, `tomtom` and `ais` keys through the
existing platform credential adapter. It never writes keys to settings, mission
documents, exports or logs. The fields do not display stored secrets. An
unavailable operating-system secret store produces an explicit error; there is no
plaintext fallback. AIS subscriptions and credentialed HTTP requests cannot
redirect credentials to a different endpoint.

Successful snapshots show actual retrieval UTC, rejected-record counts, notices
and attribution. Unknown observation times stay unknown. On provider failures,
previous records remain with an explicit stale/unavailable message. Static
reference snapshots do not acquire a fabricated observation or retrieval date.
FIRMS thermal detections are not fire perimeters; OSM context does not establish
operational state; satellite catalog metadata is not decoded imagery.

At most three background source jobs run concurrently. HTTP requests have a
20-second inactivity and 30-second total deadline, a five-redirect maximum for
uncredentialed requests, and per-source response limits between 2 and 24 MiB.
Manual cancellation uses owned request state and generation checks. Old results
cannot replace a new source configuration. In `--offline` mode remote source
requests, restored enabled sources and camera media retrieval are disabled
before transport or credential lookup starts; bundled references and local
annotations remain usable.

Mapped queries have 10° installation / 2° energy span limits and 700 / 400 result
caps, respectively. The four existing Overpass endpoints are tried in order.
Successful queries have a five-minute, 32-entry memory cache. TomTom requires a
local 0.05° view, at most nine zoom-12 tiles, a 64-tile cache and a 1,000-tile daily
budget for the app session. Failed traffic requests still consume the budget.
Coverage and observation time are never inferred from retrieval success.

## Orbits, following and retained fixes

The CelesTrak TLE parser validates checksums, identifiers, epochs and physical
parameter bounds. It calls the existing Vallado CSGP4 implementation unchanged.
Catalogs are retained for six hours; positions are propagated every 15 seconds.
Estimated orbit paths contain 97 samples over one period. Records retain the TLE
epoch separately from propagation UTC and explicitly state the estimate's age.
These are not observed positions or guaranteed trajectories.

Follow selection updates the shared map's center while retaining its view
parameters. Retained trails and playback use actual timestamped aircraft,
vessel or transit fixes from this app session: at most one hour, 240 fixes per
object and 5,000 objects. Unknown, stale, excessive-future and nonmonotonic fixes
are excluded. Playback selects the last observed fix at or before the selected
time, without interpolation or extrapolation. Orbital estimates do not become
track history. The inspector labels retained positions and current metadata.
Workspace restoration can retain a bounded pending follow target until that
source record arrives; it never substitutes another object's position.

## Camera media

Stills are fetched only on request from the admitted TfL/Fintraffic image hosts.
JPEG responses are bounded to 8 MiB; dimensions/pixel count are checked before
native decoding, and the displayed image is bounded to 1600×1200.

Operator clips are fetched without redirects, capped at 24 MiB and written to
an owned temporary file. A separate FFmpeg process decodes that local file into
bounded PPM frames displayed by a native Qt label. There is no QML, WebEngine,
JavaScript or embedded browser. Decoder arguments are passed directly without a
shell; network protocols/external MOV references are disabled. Input pixels,
individual allocations, decoder threads, frame buffers, output dimensions
(1280×720), display rate (12 frames/s) and elapsed duration are bounded. Audio is
not played. Stop/close cancels the request/owned decoder and removes the clip.
Pause/resume controls operate on the owned decoder. Seeking to an explicit clip
offset restarts decoding against the same downloaded local file without another
network retrieval. Playback offsets do not supply a sensor acquisition time.

The decoder is discovered beside the executable, then on PATH. Apple Silicon
GUI launches additionally check `/opt/homebrew/bin/ffmpeg` and
`/usr/local/bin/ffmpeg`. An unavailable decoder produces an explicit message and
does not prevent stills, sources or ordinary application startup. The Linux
package can provide its separately licensed FFmpeg executable. FFmpeg and its
codec dependencies retain their own licenses, including the configured Arch
binary's GPL-3.0-or-later terms; repository MIT licensing does not relicense them.
Mac clip playback requires a separately available compatible FFmpeg binary and
has not been verified on a Mac in this Linux session.

## Saved evidence and data notices

Add evidence creates a bounded compatible mission record with the original
source identifier, units, coordinate, optional altitude and source time. Raw
provider responses and transient camera delivery links remain outside the
mission snapshot. Numeric/boolean detail fields become textual representations
required by the existing Swift mission schema. Oversized geometry is explicitly
rejected for mission evidence instead of silently simplified; Export record
retains the admitted full source record. Rendered map geometry can be bounded
independently without changing the stored source geometry.
Opening a stored source record from Mission Control uses a separate read-only
Saved mission snapshot inspector. It retains the original source identity,
observation/epoch time (including Foundation dates) and frozen details, with a
bounded saved-JSON view. The inspector performs no source retrieval and live
updates do not replace its contents.

Bundled data and notices are deployed under `share/godeye/sources` on Linux and
`Contents/Resources/sources` on macOS. The source snapshot contains 4,351
datacenter rows, 704 dam rows and 712 cable-system / 1,917 landing-point rows.
Duplicate admission produces 4,351 datacenter, 703 dam and 2,611 cable records.
Dataset README files and the SGP4 notice accompany those resources. Datacenters
and dams retain OSM/Open Infrastructure Map ODbL notices; TeleGeography data
retains CC BY-NC-SA 3.0 and its non-commercial restriction. Acquisition dates for
the OSM-derived extracts remain unknown; the cable snapshot date is 2026-05-24.

## Verification

`Tests/DomainTests.cpp` covers feed inventory/exclusions, numbers/coordinates,
timestamps/units, malformed JSON/XML/protobuf, GTFS/MVT/AIS admission, reference
counts/deduplication, mission snapshot limits, retained histories, offline
requests and the existing 16 Vallado reference vectors. `Tests/DomainUITests.cpp`
covers actual selection/evidence controls, restored offline settings and bounded
clip-frame parsing. Real camera media is explicitly opt-in:

```sh
GODEYE_LIVE_CHECKS=1 GODEYE_DOMAIN_QA=/absolute/disposable/qa-directory \
  /path/to/GodEyeDomainUITests realOperatorStillAndNativeClip
GODEYE_LIVE_CHECKS=1 /path/to/GodEyeDomainLiveChecks /absolute/report.json
```

The live-source harness records actual source IDs, times, counts, errors and
latency. It rejects credentialed sources; AISStream, TomTom and FIRMS require
separate checks with user-supplied keys. Provider downtime is reported as a
failed/limited real check, never replaced with fixture records. Generated
fixtures are used only inside tests and disposable profiles. Tests and Linux
media checks do not close the separate Mac, multi-display or scientific
validation gates.
