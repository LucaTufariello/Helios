import { GIBS_LAYERS, gibsDateString } from '../../data/gibsLayers.js';

/**
 * Sat Fresh snapshot source (keyless, no network).
 *
 * GIBS tiles load direct from the browser like Esri — there is no proxy.
 * The snapshot only resolves WHICH date/layers the renderer should show:
 * yesterday (UTC) TrueColor by default, with Terra fallback + night lights.
 */
export function createSatFreshSource() {
  return {
    getSnapshot() {
      const date = gibsDateString(new Date(), 1);
      return {
        fetchedAt: Date.now(),
        stale: false,
        date,
        layers: {
          day: GIBS_LAYERS.trueColor.id,
          fallback: GIBS_LAYERS.terraFallback.id,
          night: GIBS_LAYERS.nightLights.id,
          fires: GIBS_LAYERS.thermalAnomalies.id,
        },
        resolutionNote: '250m — neighborhoods, not cars. Not live video.',
      };
    },
  };
}
