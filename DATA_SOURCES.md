# Native data sources and attribution

The canonical provider names and credits live in
`native/desktop/Core/DomainFeeds.cpp`. URLs, bounds and request policies live with
the native services. This table describes the maintained application;
provider terms control data use independently of the project's MIT source license.
Free access does not imply unrestricted redistribution or commercial use.

| Source | Native use | Terms and interpretation |
| --- | --- | --- |
| Esri World Imagery | Satellite basemap | Esri and imagery-provider attribution; a dated mosaic with varying acquisition dates. |
| OpenStreetMap | Street basemap, mapped infrastructure, directions | © OpenStreetMap contributors, ODbL 1.0; public tile/Overpass/routing service policies apply. Community mapping is incomplete. |
| Mapzen terrain on AWS | Terrarium elevation tiles | Bundled attribution covers the underlying elevation sources. Mixed vertical datums/vintages; not survey-grade elevation. |
| User-selected 3D Tiles / public AGI coverage | Georeferenced scene meshes | Dataset-specific terms; availability does not grant redistribution rights. Cesium Native/component notices are packaged separately. |
| NASA Worldview / GIBS | Dated daily imagery composites | NASA/source credit; coarse composites may have gaps and do not resolve individual buildings. |
| Copernicus Sentinel-1 | GRD catalogue, downloads, calibrated radar | Copernicus Sentinel data; retain acquisition, polarization, orbit and processing provenance. Radar backscatter is not optical photography. |
| Copernicus Sentinel-2 / Earth Search, Element 84 | Dated Level-2A optical imagery | Copernicus Sentinel data; RGB source pixels are 10 m. Tile cloud percentage is not a per-pixel clear-sky guarantee. |
| CBERS-4A WPM public catalogue | Higher-detail optical option where available | INPE/CBERS and catalogue-provider terms/credit; 2 m pan and 8 m multispectral are distinct products, with incomplete coverage. |
| PRISMA imported L2D | Local panchromatic/hyperspectral viewing | ASI/PRISMA terms for the user's product; no direct ASI ordering/download integration. 5 m pan and 30 m hyperspectral have different purposes. |
| USGS | Recent earthquakes | Data courtesy of the U.S. Geological Survey. |
| OpenSky Network | Civil aircraft reports | OpenSky non-commercial terms; availability/rate limits apply. |
| adsb.lol | Regional civil fallback and military aircraft reports | ODbL 1.0; public ADS-B reception is incomplete. |
| AISStream | Vessel position messages | Provider terms and user key; reception gaps and unavailable fields stay explicit. |
| CelesTrak | TLE satellite catalogs | CelesTrak attribution; native SGP4/SDP4 positions are propagated estimates, not observed fixes. |
| Launch Library 2 / The Space Devs | Launch-site and mission metadata | Source attribution and provider policy; launch records do not locate vehicles in flight. |
| Open-Meteo / CAMS | Weather and air-quality models | CC BY 4.0 and free-service non-commercial terms; model readouts are not station measurements. |
| GDACS | Disaster alerts | UN / EU JRC source credit; public alert data. |
| NASA FIRMS | Satellite thermal detections | NASA/FIRMS citation, provider MAP_KEY; detections are not confirmed fire perimeters. |
| TomTom | Bounded relative road-flow tiles | Provider terms/key/quotas; flow estimates do not identify individual vehicles. |
| TfL / Fintraffic | Public traffic-camera catalog/media | Operator terms/credits; stills and clips may be delayed, with missing capture timestamps. |
| Public GTFS-realtime operators | Vehicle positions | Per-operator attribution and license are retained in the canonical JSON registry. |

## Canonical bundled resources

`native/resources/` is the only maintained copy of the
reference data. The CMake build stages these files into the application:

- `datacenters/`: 4,351 OpenStreetMap-derived features, ODbL 1.0. Extraction date
  and original query were not recorded; future updates must record them.
- `dams/`: 704 raw features from OpenInfraMap/OSM-derived data; the native loader
  admits 703 after duplicate rejection. ODbL 1.0 and source attribution apply.
- `telegeography_submarine_cables/`: 712 raw cable features and 1,917 landing points,
  downloaded 24 May 2026. The loader admits 694 cable systems with geometry.
  **© TeleGeography — submarinecablemap.com, CC BY-NC-SA 3.0.** Commercial use requires
  removing the dataset or obtaining a separate license; adaptations retain its terms.
- `transit-feeds.json`: seven admitted public operator feeds with license URLs,
  attribution, allowed headers and geographic bounds. Rome ATAC remains excluded
  under the existing terms decision.
- `terrain-attribution.md`: source-specific terrain credits.
- `SGP4-LICENSE.txt`: orbital-engine source license, separate from data licenses.

Reference geometry describes a static dataset snapshot and does not establish
current operational status. Resource READMEs retain detailed provenance and terms.
See [resource maintenance](native/resources/README.md), [NOTICE.md](NOTICE.md)
and the satellite guides for processing-specific provenance and limitations.
