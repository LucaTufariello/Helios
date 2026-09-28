/**
 * @file Pure styling helpers for live traffic flow — level → bucket/color/
 * and color. Cesium-free so thresholds are unit-testable; the traffic
 * layer maps buckets to Cesium colors at spawn time.
 *
 * `level` is TomTom's `traffic_level`: current speed / free-flow speed,
 * 0..1 where 1 = free flow. Renderers must omit records without finite flow data. The neutral
 * fallback in this helper does not establish an observation.
 *
 * @module data/trafficFlowStyle
 */

/** @const {number} Levels at/above this render as free-flowing (green). */
const FREE_THRESHOLD = 0.85;
/** @const {number} Levels at/above this (and below FREE) render slow (amber); below is jam (red). */
const SLOW_THRESHOLD = 0.55;

/**
 * Bucket color palette as Cesium-free rgba tuples ([r, g, b] 0–255 + alpha 0–1):
 * green #2ecc71 / amber #f0b23e / red #e05252, all at 0.9 alpha.
 * @type {{free:number[], slow:number[], jam:number[]}}
 */
export const FLOW_BUCKET_RGBA = {
  free: [46, 204, 113, 0.9],
  slow: [240, 178, 62, 0.9],
  jam: [224, 82, 82, 0.9],
};

/**
 * Classify a traffic level into a congestion bucket.
 * @param {number} level - traffic_level 0..1 (1 = free flow).
 * @returns {'free'|'slow'|'jam'} Bucket name; non-finite input → 'free'.
 */
export function flowBucket(level) {
  if (!Number.isFinite(level)) return 'free';
  if (level >= FREE_THRESHOLD) return 'free';
  if (level >= SLOW_THRESHOLD) return 'slow';
  return 'jam';
}

/**
 * rgba tuple for a traffic level (bucket palette lookup).
 * @param {number} level - traffic_level 0..1.
 * @returns {number[]} [r, g, b, a] with rgb 0–255, alpha 0–1.
 */
export function flowColor(level) {
  return FLOW_BUCKET_RGBA[flowBucket(level)];
}
