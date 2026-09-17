import * as Cesium from 'cesium';
import { TRAJECTORY_STAGE_COLORS } from './policy.js';

export function createRendering({
  state: layerState,
  services,
  parts,
  source,
}) {
  const { getSatelliteOrbitTrack, findSatelliteOrbitTrackInTle } =
    services.satellites;

  function setGraphicVisibility(graphic, visible, time) {
    if (!graphic) return;
    const next = Boolean(visible);
    const current = graphic.show?.getValue?.(time);
    if (current !== next) graphic.show = next;
  }

  function updateMissionFrame() {
    if (
      !layerState._enabled ||
      !layerState._viewer ||
      !layerState._dataSource?.show
    ) {
      return;
    }
    parts.panel.updateMissionTelemetry();
    const time = Cesium.JulianDate.now(layerState._declutterTime);
    layerState._declutterOccluder.cameraPosition =
      layerState._viewer.scene.camera.positionWC;
    parts.overlays.refreshSelectedMissionOverlayText();
    for (const entity of layerState._dataSource.entities.values) {
      if (!entity.show) continue;
      if (!entity.position) continue;
      const id = parts.selection.entityLaunchId(entity);
      if (!id) continue;
      const position = entity.position.getValue(time);
      let horizonVisible = Boolean(
        position && layerState._declutterOccluder.isPointVisible(position),
      );
      const launchAnchor = entity.id.startsWith('rocket-launch:');
      if (launchAnchor) {
        horizonVisible = parts.overlays.missionAnchorVisible(
          layerState._viewer.scene.camera.positionWC,
          position,
          id,
          layerState._selectedLaunchId,
        );
        // Keep entity.show reserved for mission-selection isolation. Horizon
        // state is applied to the individual graphics below so a rear-side
        // anchor remains eligible for reevaluation as the camera moves.
      }
      if (entity.point) {
        setGraphicVisibility(entity.point, horizonVisible, time);
      }
      if (launchAnchor && entity.billboard) {
        setGraphicVisibility(
          entity.billboard,
          horizonVisible && id === layerState._hoveredRosterLaunchId,
          time,
        );
      }
    }
  }

  function addLaunchEntity(launch, activeTleText = layerState._activeTleText) {
    const position = Cesium.Cartesian3.fromDegrees(launch.lon, launch.lat);
    const overlayRecord = {
      launch,
      anchorPosition: position,
      elementEntryFactories: [],
      liveEventTime: null,
    };
    layerState._missionOverlayRecords.set(launch.id, overlayRecord);
    const orbitAllowed = parts.policyHelpers.launchStatusAllowsOrbit(
      launch.status,
    );
    const coreTrack =
      orbitAllowed && launch.satelliteQuery
        ? getSatelliteOrbitTrack(launch.satelliteQuery, {
            launchTime: launch.launchTime,
          })
        : null;
    const satelliteTrack =
      coreTrack ||
      (orbitAllowed && activeTleText && launch.satelliteQuery
        ? findSatelliteOrbitTrackInTle(activeTleText, launch.satelliteQuery, {
            launchTime: launch.launchTime,
          })
        : null);
    const orbitPath = !orbitAllowed
      ? null
      : satelliteTrack?.orbitPath?.length > 1
        ? satelliteTrack.orbitPath
        : parts.paths.approximateOrbitPath(launch);
    launch.recoveryStages.forEach((stage) => {
      stage.endpoint = parts.paths.landingEndpoint(
        stage,
        launch,
        orbitPath?.[0] || null,
      );
    });
    if (satelliteTrack) layerState._orbitMatches++;
    const entity = layerState._dataSource.entities.add({
      id: `rocket-launch:${launch.id}`,
      position,
      point: {
        pixelSize: 5,
        color: parts.model.missionMarkerColor(launch),
        outlineColor: parts.model.missionMarkerColor(launch),
        outlineWidth: 0,
        disableDepthTestDistance: Number.POSITIVE_INFINITY,
      },
      billboard: {
        image: parts.overlays.missionHoverReticleImage(),
        width: 24,
        height: 24,
        show: false,
        horizontalOrigin: Cesium.HorizontalOrigin.CENTER,
        verticalOrigin: Cesium.VerticalOrigin.CENTER,
        disableDepthTestDistance: Number.POSITIVE_INFINITY,
      },
      properties: {
        launchId: launch.id,
        launchName: launch.name,
        launchTime: launch.launchTime,
        launchSite: launch.launchSite,
        provider: launch.provider,
        mission: launch.mission,
        status: launch.status,
        source: launch.source,
        orbit: JSON.stringify(launch.orbit || {}),
        satelliteName: satelliteTrack?.name || '',
        noradId: satelliteTrack?.noradId || '',
      },
    });
    const points = launch.trajectory
      .filter(
        (point) =>
          Number.isFinite(Number(point.latitude)) &&
          Number.isFinite(Number(point.longitude)),
      )
      .map((point) => ({
        stage: String(
          point.stage ||
            point.stage_name ||
            point.phase ||
            point.stageName ||
            'trajectory',
        ),
        position: Cesium.Cartesian3.fromDegrees(
          Number(point.longitude),
          Number(point.latitude),
          Number(point.altitude || 0),
        ),
      }));
    if (points.length > 1) {
      const segments = [];
      points.forEach((point) => {
        const previous = segments.at(-1);
        if (!previous || previous.stage !== point.stage)
          segments.push({ stage: point.stage, positions: [] });
        segments.at(-1).positions.push(point.position);
      });
      segments.forEach((segment, index) => {
        if (segment.positions.length < 2) return;
        layerState._dataSource.entities.add({
          id: `rocket-trajectory:${launch.id}:${index}`,
          polyline: {
            positions: parts.paths.surfaceSafePath(segment.positions),
            width: 2,
            material: Cesium.Color.fromCssColorString(
              TRAJECTORY_STAGE_COLORS[index % TRAJECTORY_STAGE_COLORS.length],
            ).withAlpha(0.8),
            clampToGround: false,
            arcType: Cesium.ArcType.NONE,
          },
          properties: {
            launchId: launch.id,
            stage: segment.stage,
            source: launch.source,
          },
        });
      });
    }
    if (orbitPath?.length > 1) {
      const orbitCurrent = satelliteTrack?.current || {
        longitude: launch.lon,
        latitude: launch.lat,
        altitude: 0,
      };
      const orbitPeriodSec =
        satelliteTrack?.periodSec ||
        parts.paths.estimatedOrbitPeriodSeconds(orbitPath);
      const launchEpochMs = Date.parse(launch.launchTime);
      const disclosedInsertionOffsetSec =
        parts.paths.orbitInsertionOffsetSeconds(launch);
      const insertionDurationSec = Number.isFinite(disclosedInsertionOffsetSec)
        ? Math.max(0, disclosedInsertionOffsetSec)
        : 600;
      const insertionEpochMs = Number.isFinite(launchEpochMs)
        ? launchEpochMs + insertionDurationSec * 1000
        : Number.NaN;
      let insertionReference = points.at(-1)?.position || null;
      if (
        !insertionReference &&
        satelliteTrack?.positionAt &&
        Number.isFinite(insertionEpochMs)
      ) {
        const propagatedInsertion = satelliteTrack.positionAt(
          new Date(insertionEpochMs),
        );
        if (propagatedInsertion) {
          insertionReference = Cesium.Cartesian3.fromDegrees(
            propagatedInsertion.longitude,
            propagatedInsertion.latitude,
            propagatedInsertion.altitude,
          );
        }
      }
      if (!insertionReference && !satelliteTrack) {
        // A projected orbit has no authoritative historical phase. Start its
        // plane over the launch site, advance only by the estimated powered
        // ascent duration, and join the forward orbit tangent. Using the UTC
        // epoch as phase sent some ascents toward the far side of Earth and
        // forced a visible 180-degree corrective hook before insertion.
        const insertionProgress = Cesium.Math.clamp(
          insertionDurationSec / orbitPeriodSec,
          1 / 96,
          0.16,
        );
        insertionReference = parts.paths.samplePath(
          orbitPath,
          insertionProgress,
        );
      }
      // Orbit label anchors near the insertion side of the real orbit ring.
      const orbitLabelBakePosition =
        orbitPath[
          parts.paths.nearestOrbitIndex(
            orbitPath,
            insertionReference || position,
          )
        ] || position;
      let livePosition = Cesium.Cartesian3.fromDegrees(
        orbitCurrent.longitude,
        orbitCurrent.latitude,
        orbitCurrent.altitude,
      );
      const liveTime = new Date();
      const liveTelemetry = { speedMps: null };
      layerState._satelliteTelemetry.set(launch.id, liveTelemetry);
      let liveFrameNumber = -1;
      const fallbackPeriodSec =
        parts.paths.estimatedOrbitPeriodSeconds(orbitPath);
      const updateLiveState = () => {
        const frameNumber =
          layerState._viewer?.scene?.frameState?.frameNumber ?? -1;
        if (frameNumber !== -1 && frameNumber === liveFrameNumber) return;
        liveFrameNumber = frameNumber;
        const nowMs = Date.now();
        liveTime.setTime(nowMs);
        if (satelliteTrack) {
          const propagated =
            satelliteTrack.positionAt?.(liveTime) || satelliteTrack.current;
          if (propagated) {
            Cesium.Cartesian3.fromDegrees(
              propagated.longitude,
              propagated.latitude,
              propagated.altitude,
              undefined,
              livePosition,
            );
            liveTelemetry.speedMps = Number.isFinite(propagated.speedMps)
              ? propagated.speedMps
              : null;
          }
        } else {
          // In-place write keeps one Cartesian3 for the mission's lifetime —
          // the estimated branch previously reallocated per propagation.
          livePosition =
            parts.paths.samplePath(
              orbitPath,
              parts.paths.orbitProgressAtTime(nowMs, fallbackPeriodSec),
              livePosition,
            ) || livePosition;
        }
      };
      layerState._dataSource.entities.add({
        id: `rocket-satellite:${launch.id}`,
        position: new Cesium.CallbackProperty(() => {
          updateLiveState();
          return livePosition;
        }, false),
        point: {
          pixelSize: 6,
          color: Cesium.Color.fromCssColorString(
            satelliteTrack ? '#7bed9f' : '#ffd166',
          ),
          outlineWidth: 0,
          disableDepthTestDistance: Number.POSITIVE_INFINITY,
        },
        properties: {
          launchId: launch.id,
          noradId: satelliteTrack?.noradId || '',
          phase: satelliteTrack
            ? 'CURRENT_SATELLITE_POSITION'
            : 'ESTIMATED_ORBIT_POSITION',
          source: satelliteTrack
            ? 'Satellites layer'
            : 'Approximate mission orbit',
        },
      });
      overlayRecord.liveEventTime = () => liveTime;
      overlayRecord.elementEntryFactories.push(() => {
        const name = satelliteTrack
          ? parts.overlays
              .shortMissionLabel(satelliteTrack.name, 22)
              .toUpperCase()
          : 'EST. ORBIT POSITION';
        return parts.overlays.createRocketMissionElementOverlayEntry({
          id: `payload-position:${launch.id}`,
          position: () => livePosition,
          text: `${name}\n${parts.policyHelpers.formatMissionEventTime(liveTime)}`,
          accent: satelliteTrack ? '#7bed9f' : '#ffd166',
          priority: 900_000,
          gapPx: 12,
        });
      });
      // Live satellite rings use a primitive collection so the core Satellite
      // GMST transform can rotate their baked ECEF geometry without rebuilding
      // it. Estimated launch-site-relative rings remain ordinary entities.
      const primitiveOrbitAdded = parts.orbitRendering.addMissionOrbitPrimitive(
        launch,
        orbitPath,
        satelliteTrack,
      );
      if (!primitiveOrbitAdded) {
        layerState._dataSource.entities.add({
          id: `rocket-orbit:${launch.id}`,
          polyline: {
            positions: orbitPath,
            // The 3 px canvas gives the periodic dot room to read as round; the
            // shader masks the ten intervening dashes down to a thin center stroke.
            width: 3,
            material:
              new parts.orbitRendering.MissionOrbitPatternMaterialProperty(
                Cesium.Color.fromCssColorString('#c084fc').withAlpha(0.95),
              ),
            arcType: Cesium.ArcType.NONE,
          },
          properties: {
            launchId: launch.id,
            satelliteName: '',
            noradId: '',
            source: 'Approximate mission orbit',
          },
        });
      }
      let orbitLabelPosition = orbitLabelBakePosition;
      const primitivePath = layerState._missionOrbitPrimitives.get(launch.id);
      if (primitivePath) {
        primitivePath.labelBakePosition = orbitLabelBakePosition;
        primitivePath.labelPosition = Cesium.Matrix4.multiplyByPoint(
          primitivePath.primitive.modelMatrix,
          orbitLabelBakePosition,
          new Cesium.Cartesian3(),
        );
        orbitLabelPosition = () => primitivePath.labelPosition;
      }
      overlayRecord.elementEntryFactories.push(() =>
        parts.overlays.createRocketMissionElementOverlayEntry({
          id: `orbit:${launch.id}`,
          position: orbitLabelPosition,
          text: satelliteTrack ? 'ORBIT' : 'PROJECTED ORBIT',
          accent: satelliteTrack ? '#22e6e6' : '#c084fc',
          priority: 800_000,
          gapPx: 8,
        }),
      );
    }
  }
  return { setGraphicVisibility, updateMissionFrame, addLaunchEntity };
}
