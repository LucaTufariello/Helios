# NASA daily visualization layer

`Core/DailyImagery.{h,cpp}` ports the existing Swift `Imagery.swift` choices:
Off, VIIRS true colour, Terra true colour, night lights and thermal anomalies.
The cache starts **Off** and sends no startup requests. The map chooses an
explicit UTC calendar date. That date identifies the requested daily product;
it is not every pixel's acquisition time. These are provider visualization
tiles, not full-resolution scientific raster exports or newly measured change.
Clouds, coverage gaps, missing dates and transparent pixels remain possible.

NASA documents the fixed WMTS service, time dimension and projection in its
[GIBS access guide](https://nasa-gibs.github.io/gibs-api-docs/access-basics/).
The implementation uses only HTTPS `gibs.earthdata.nasa.gov`, the EPSG:3857
`best` endpoint and the existing four allowlisted layer identifiers. URLs contain
validated canonical tile coordinates and a real ISO calendar date. The true-colour
matrix set uses level 9, ENCC night lights uses level 8, and thermal visualization
is capped at level 7. A requested high map zoom reuses the source's maximum-level
parents; it does not imply higher source resolution. Future dates, invalid dates,
dates before 2000, unknown source choices and noncanonical tiles are rejected.
Different products' actual start dates are provider availability, not invented
by the client.

Actual NASA [service capabilities](https://gibs.earthdata.nasa.gov/wmts/epsg3857/best/1.0.0/WMTSCapabilities.xml)
on 2026-10-15 expose ENCC night lights as an archive ending 2023-07-07. Selecting
a current date can therefore be unavailable; the cache does not silently substitute
another date or another radiance product. The source label identifies this archive.
Thermal WMTS is currently advertised as vector tiles. The cache requests NASA's
WMS PNG rendering of the same thermal layer/date with the exact EPSG:3857 tile
bounding box, fixed 256×256 output and transparency, preserving the raster map
workflow. These are corrections to the old Swift universal Level9/PNG URLs,
which did not work for these two sources in the actual service check.

`DailyImageryCache(directory, networking, parent, optionalNetworkManager)` exposes:

- `configure(DailySource, QDate)` to select a source/date, cancel old requests and
  reset visible state; `source()`, `date()` and `generation()` identify it.
- `request(QVector<TileID>)` to select viewport tiles. Requests deduplicate, retain
  useful parents, cap provider zoom at 9 and discard unneeded viewport requests.
- `image(TileID)` to return a retained `shared_ptr<DailyTile>` at the closest
  available level. `DailyTile::id` is the actual returned parent and must be used
  for geometry/UVs; `image` contains RGBA pixels and `revision` identifies updates.
- `busy()`, `pending()`, `failures()`, `cached()` and `changed` for honest map status.
  Static `sourceID/readSource/sourceName/resolution/url/decode` support UI/storage
  and admission tests without graphics dependencies. `maximumZoom(source)`
  exposes the source-specific cap.

At most four combined fetch/decode operations run concurrently. Each response is
limited to 2 MiB, decoded off the UI thread, and admitted only as a 256×256 PNG or
JPEG. Colour is converted to sRGB RGBA while transparency is retained. In-memory
decoded tiles have a hard 128-tile/32 MiB pixel budget; the provider disk cache
has a 128 MiB limit. Active network/decode buffers are additional bounded transient
memory. Viewport input is capped at 64 tiles and the deduplicated parent/fine
request set at 224 entries. Network requests use 20-second inactivity and
30-second wall deadlines, no cookies and no redirects. Failed current tiles
have a 30-second retry backoff. Source/date generation checks discard stale
network and decode results, including when the old background decode completes
after a new source is selected. Offline mode makes no requests and reports no
invented imagery.

Renderer integration must namespace textures by source/date or generation, use
the returned parent's tile ID when sampling and repaint on `changed`. Changing
the date clears previous source imagery before presenting the new date. The map
should display NASA GIBS attribution, the requested UTC date, approximate source
resolution and the daily-product disclosure beside this layer. Thermal tiles
are provider detections; showing them does not establish construction or damage.

`Tests/DailyImageryTests.cpp` checks URL/date/source/tile admission, image bounds,
bad formats, transparency, default Off/offline behavior, disk budget, parent reuse,
four-request concurrency, viewport cancellation, stale generations including an
in-flight decode, HTTP errors/redirects/oversized replies, retry backoff, RAM cache
cap and high-zoom reuse. Live tests are opt-in:

```sh
HELIOS_DAILY_LIVE=1 HELIOS_DAILY_QA=/absolute/path/to/qa \
  ./output/desktop/build/native/desktop/HeliosDailyImageryTests
```

On 2026-10-15, an actual Terra request for UTC product date 2026-10-12 retrieved
tile `6/33/22` and its six parents from NASA GIBS: seven retained tiles, zero
failed tiles, a decoded 256×256 fine tile. The saved image was inspected and shows
the Alps/Po region. The report, image and test log are retained under
`output/qa/linux-migration-20261015/daily-imagery/` (ignored development output).
The other source checks also passed: VIIRS on 2026-10-12, ENCC archive on
2023-07-07, and thermal WMS on 2026-10-12. The thermal fine tile had 65,536
transparent pixels out of 65,536; that empty visualization is retained honestly,
not turned into positive detections. True-colour and archive night-light images
were visually inspected. Twelve live-suite checks passed without skips. The
ASAN/UBSAN core suite passed ten checks with the two network cases explicitly
skipped; no sanitizer findings were reported.
This establishes public service/decoder/cache operation on Linux. Integration
into the main map, screenshots of that map and the Mac path require their own
checks; a successful tile fetch alone does not close those gates.
