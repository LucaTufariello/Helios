import { createGdacsLayer } from '../../layers/gdacs/index.js';
import { createGdacsSource } from '../../layers/gdacs/source.js';
import { overlayHost } from './overlayHost.js';

/** Construct one Disaster Alerts layer (GDACS, public domain). */
export function createApplicationGdacs(options = {}) {
  return createGdacsLayer({
    overlayHost,
    source: options.source ?? createGdacsSource(),
  });
}
