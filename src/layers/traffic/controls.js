import { TRAFFIC_TIMING_ENABLED } from './policy.js';

export function createControls({ state: layerState, services, parts, source }) {
  const { getFlowSessionStats } = source;

  const methods = {
    id: 'traffic',

    name: 'Road Conditions',

    icon: '🚗',

    source: 'TomTom flow · OpenStreetMap roads',

    // The one data-layer control a provider key gates: without a TomTom key
    // the layer loads nothing (KEY REQUIRED) instead of simulating traffic.
    requiresKeyId: 'tomtom',

    /** Refresh measured flow while parked; cached road geometry is reused. */
    updateInterval: 120000,

    // Aggregate flow has no observed individual vehicle positions.
    getDetectableObjects() {
      return [];
    },

    getRowControls() {
      if (!layerState._count || layerState._flowError) return { legend: [] };
      return {
        legend: [
          {
            label: 'Free flow',
            count: layerState._bucketCounts.free,
            color: '#64b98b',
            blurb: 'At least 85% of free-flow speed',
          },
          {
            label: 'Slow',
            count: layerState._bucketCounts.slow,
            color: '#d7b461',
            blurb: '55–85% of free-flow speed',
          },
          {
            label: 'Congested',
            count: layerState._bucketCounts.jam,
            color: '#d97968',
            blurb: 'Below 55% of free-flow speed',
          },
          {
            label: 'Closed',
            count: layerState._closedRoads,
            color: '#b992d1',
            blurb: 'Closure reported by source',
          },
        ],
      };
    },

    getStats() {
      // Outstanding flow work counts as loading: the paint race can leave a
      // TomTom request in flight after the roads have settled, and the shared
      // loading batch has to stay open long enough to announce its failure.
      const loading = layerState._fetching || layerState._flowPending > 0;
      const feed = parts.model.trafficFeedPresentation({
        liveMode: layerState._liveMode,
        fetching: loading,
        flowError: layerState._flowError,
        coveragePct: layerState._flowCoveragePct,
        statusUnavailable: layerState._flowStatusUnavailable,
        keyRequired: layerState._keyRequired,
      });
      return {
        count: layerState._count,
        countUnit: 'road segments',
        stale: Boolean(
          layerState._lastUpdate &&
          Date.now() - layerState._lastUpdate > 5 * 60_000,
        ),
        partial: layerState._truncated === true,
        coverage: 'Aggregate road flow; no individual vehicle positions',
        lastUpdate: layerState._lastUpdate,
        loading,
        mode: feed.mode,
        keyRequired: layerState._keyRequired,
        error: layerState._keyRequired
          ? 'KEY REQUIRED'
          : layerState._roadError || feed.error,
        flowCoveragePct: layerState._flowCoveragePct,
        tilesFetched: getFlowSessionStats().tilesFetched,
        ...(TRAFFIC_TIMING_ENABLED
          ? { trafficTiming: parts.timing.getTrafficTimingDiagnostics() }
          : {}),
        flowBuckets: { ...layerState._bucketCounts },
        closedRoads: layerState._closedRoads,
        heatLines: layerState._heatLineCount,
        loadingLabel: feed.loadingLabel,
      };
    },
  };

  return { methods };
}
