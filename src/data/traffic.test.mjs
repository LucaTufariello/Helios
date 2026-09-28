// src/data/traffic.test.mjs
// Feed-state honesty for the traffic layer.
//
// The layer is TomTom-key-only: keyless it loads nothing and reports KEY
// REQUIRED instead of simulating traffic. Every surface it drives — the
// toggle chip, the panel meta line, the traffic sync chip — has to say so.
// The two pure helpers below own that contract; the layer's getStats() is a
// thin caller.
import test from 'node:test';
import assert from 'node:assert/strict';
import trafficLayer, {
  deriveTrafficFlowError,
  trafficFeedPresentation,
} from './traffic.js';
import { DataLayerManager, layerFeedState } from './manager.js';

/**
 * The app's live markers. Case-SENSITIVE on purpose: uppercase LIVE/GPS is
 * how this UI asserts a real feed ("LIVE · TomTom flow", the old "initiating
 * global GPS sync"), while lowercase "add TomTom key for live" names the
 * remedy without claiming one.
 */
const LIVE_CLAIM = /\bLIVE\b|\bGPS\b|\breal[- ]?time\b/;

test('a superseded flow fetch is not an outage', () => {
  assert.equal(deriveTrafficFlowError({ name: 'AbortError', message: 'aborted' }), null);
  assert.equal(deriveTrafficFlowError(null), null);
  assert.equal(deriveTrafficFlowError(undefined), null);
});

test('flow failures map onto short, specific reasons', () => {
  const reason = (message) => deriveTrafficFlowError(new Error(message));
  assert.equal(reason('flow tile 12/1/1: HTTP 503'), 'TomTom key unavailable');
  assert.equal(reason('flow tile 12/1/1: HTTP 429'), 'TomTom daily budget reached');
  assert.equal(reason('flow tile 12/1/1: HTTP 502'), 'TomTom upstream unreachable');
  assert.equal(reason('flow tile 12/1/1: HTTP 504'), 'TomTom upstream unreachable');
  assert.equal(reason('flow tile 12/1/1: HTTP 418'), 'TomTom flow error (HTTP 418)');
  assert.equal(reason('flow fetch failed'), 'TomTom flow unavailable');
});

test('keyless traffic reports KEY REQUIRED, loading or idle', () => {
  const idle = trafficFeedPresentation({ liveMode: false, fetching: false, keyRequired: true });
  const loading = trafficFeedPresentation({ liveMode: false, fetching: true, keyRequired: true });
  assert.equal(idle.mode, 'unavailable');
  assert.equal(loading.mode, 'unavailable');
  // Keyless is a missing key, not a fault in a feed — but it must read as a
  // named, actionable state, never as an empty healthy layer.
  assert.equal(idle.error, 'KEY REQUIRED');
  assert.equal(loading.error, 'KEY REQUIRED');
  assert.equal(idle.loadingLabel, 'KEY REQUIRED — add TomTom key for live');
  assert.equal(loading.loadingLabel, 'KEY REQUIRED — add TomTom key for live');
});

test('no keyless label ever implies a live feed', () => {
  const labels = [
    trafficFeedPresentation({ liveMode: false, fetching: false, keyRequired: true }),
    trafficFeedPresentation({ liveMode: false, fetching: true, keyRequired: true }),
    trafficFeedPresentation({ statusUnavailable: true }),
    trafficFeedPresentation({ liveMode: true, flowError: 'TomTom flow unavailable' }),
    trafficFeedPresentation({ liveMode: true, fetching: true, flowError: 'TomTom flow unavailable' }),
  ].map((feed) => feed.loadingLabel);
  for (const label of labels) {
    assert.ok(!LIVE_CLAIM.test(label), `label implies live data: ${label}`);
  }
  assert.ok(
    labels[0].startsWith('KEY REQUIRED'),
    `keyless label must lead with the remedy: ${labels[0]}`,
  );
});

test('an unreachable status probe blocks loads distinctly from KEY REQUIRED', () => {
  const probeDown = trafficFeedPresentation({ statusUnavailable: true });
  assert.equal(probeDown.mode, 'unavailable');
  assert.equal(probeDown.error, 'traffic service unreachable');
  assert.equal(probeDown.loadingLabel, 'traffic service unreachable');
});

test('a healthy keyed layer reports live flow with its real coverage', () => {
  const idle = trafficFeedPresentation({ liveMode: true, coveragePct: 87 });
  assert.deepEqual(idle, {
    mode: 'live',
    error: null,
    loadingLabel: 'TomTom flow · 87% roads matched',
  });
  assert.equal(
    trafficFeedPresentation({ liveMode: true, fetching: true }).loadingLabel,
    'Refreshing road flow',
  );
});

test('a mid-session flow outage degrades instead of reporting stale live coverage', () => {
  const down = trafficFeedPresentation({
    liveMode: true,
    flowError: 'TomTom daily budget reached',
    coveragePct: 87, // last-good number — must not be presented as current
  });
  // error and loadingLabel are ONE string: the manager's error branch renders
  // `error` and drops `loadingLabel`, so the copy has to live in both.
  assert.equal(down.error, 'FLOW OFFLINE — TomTom daily budget reached');
  assert.equal(down.loadingLabel, down.error);
  assert.ok(!down.loadingLabel.includes('87'));
  const busy = trafficFeedPresentation({
    liveMode: true,
    fetching: true,
    flowError: 'TomTom daily budget reached',
  });
  assert.deepEqual(busy, down, 'the degraded state reads the same whether or not a load is in flight');
});

test('the rendered steady-state meta line carries the KEY REQUIRED copy', () => {
  const mgr = new DataLayerManager({});
  const fresh = (feed) => ({ count: 0, lastUpdate: null, ...feed });
  const settled = (feed) => ({ count: 544, lastUpdate: Date.now(), ...feed });
  assert.equal(
    mgr._buildMetaText({
      source: 'OpenStreetMap',
      stats: fresh(trafficFeedPresentation({ liveMode: false, keyRequired: true })),
    }),
    'UNAVAILABLE · OpenStreetMap · KEY REQUIRED',
  );
  assert.equal(
    mgr._buildMetaText({
      source: 'OpenStreetMap',
      stats: settled(trafficFeedPresentation({
        liveMode: true,
        flowError: 'TomTom daily budget reached',
      })),
    }),
    'DEGRADED · OpenStreetMap · FLOW OFFLINE — TomTom daily budget reached',
  );
});

test('the manager reads keyless as UNAVAILABLE and an outage as DEGRADED', () => {
  const settled = { count: 4200, lastUpdate: Date.now() };
  assert.equal(
    layerFeedState({ count: 0, lastUpdate: null, ...trafficFeedPresentation({ liveMode: false, keyRequired: true }) }),
    'unavailable',
  );
  assert.equal(
    layerFeedState({ ...settled, ...trafficFeedPresentation({ liveMode: true }) }),
    'nominal',
  );
  assert.equal(
    layerFeedState({
      ...settled,
      ...trafficFeedPresentation({ liveMode: true, flowError: 'TomTom flow unavailable' }),
    }),
    'degraded',
  );
});

test('the shipped layer boots keyless-honest before any status check', () => {
  const stats = trafficLayer.getStats();
  assert.equal(stats.mode, 'unavailable');
  assert.equal(stats.keyRequired, true);
  assert.equal(stats.error, 'KEY REQUIRED');
  assert.ok(!LIVE_CLAIM.test(stats.loadingLabel), `boot label implies live data: ${stats.loadingLabel}`);
  assert.equal(layerFeedState(stats), 'unavailable');
});

test('traffic can be destroyed before its first enable and destroyed repeatedly', async () => {
  const { default: traffic } = await import('./traffic.js');
  const viewer = { camera: { changed: { removeEventListener() {} } } };
  assert.doesNotThrow(() => traffic.destroy(viewer));
  assert.doesNotThrow(() => traffic.destroy(viewer));
  assert.equal(traffic.getStats().count, 0);
});
