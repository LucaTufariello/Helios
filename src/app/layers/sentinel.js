import { createSentinelLayer } from '../../layers/sentinel/index.js';
import { createSentinelSource } from '../../layers/sentinel/source.js';
import { overlayHost } from './overlayHost.js';

/** Construct one Sentinel free layer (latest-clear S2/S1, graceful fallback). */
export function createApplicationSentinel(options = {}) {
  return createSentinelLayer({
    overlayHost,
    source: options.source ?? createSentinelSource(),
  });
}
