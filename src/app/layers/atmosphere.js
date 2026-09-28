import { createAtmosphereLayer } from '../../layers/atmosphere/index.js';
import { createAtmosphereSource } from '../../layers/atmosphere/source.js';
import { overlayHost } from './overlayHost.js';

/** Construct one Atmosphere layer (Open-Meteo readout, graceful fallback). */
export function createApplicationAtmosphere(options = {}) {
  return createAtmosphereLayer({
    overlayHost,
    source: options.source ?? createAtmosphereSource(),
  });
}
