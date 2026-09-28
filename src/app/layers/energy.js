import { createEnergyLayer } from '../../layers/energy/index.js';
import { createEnergySource } from '../../layers/energy/source.js';
import { overlayHost } from './overlayHost.js';

/** Construct one Energy & Refining layer (OSM plants/substations/refineries). */
export function createApplicationEnergy(options = {}) {
  return createEnergyLayer({
    overlayHost,
    source: options.source ?? createEnergySource(),
  });
}
