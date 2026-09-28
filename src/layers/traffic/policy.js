/* Road geometry and observed aggregate flow, bounded to the viewport. */

export const OVERPASS_URL = '/api/overpass';

export const ACTIVATION_ALTITUDE = 8000;

export const FAST_FETCH_ALTITUDE = 4500;

export const FETCH_DEBOUNCE = 320;

export const DOT_HEIGHT_OFFSET = 3.0;

export const OVERLAP_THRESHOLD = 0.6;

export const MAX_WAYPOINTS_PER_ROAD = 80;

export const MIN_CENTER_SHIFT_KM = 0.35;

export const MAX_LOOKAT_PULL_KM = 12;

export const TILE_CACHE_MAX_ENTRIES = 64;

export const FLOW_RENDER_RACE_MS = 250;

/** Development-only diagnostics; removed by Vite from production. */
export const TRAFFIC_TIMING_ENABLED =
  import.meta.env?.DEV &&
  typeof window !== 'undefined' &&
  new URLSearchParams(window.location.search).get('trafficDebug') === '1';
