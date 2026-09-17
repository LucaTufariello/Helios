import { createSatFreshLayer } from '../../layers/satFresh/index.js';
import { createSatFreshSource } from '../../layers/satFresh/source.js';

/**
 * Construct one Sat Fresh layer (keyless NASA GIBS + terminator).
 * Sourceless from the catalog's view: the snapshot resolves locally
 * (yesterday's date), tiles load direct from GIBS like Esri.
 */
export function createApplicationSatFresh() {
  return createSatFreshLayer({ source: createSatFreshSource() });
}
