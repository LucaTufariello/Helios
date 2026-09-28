import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { runInNewContext } from 'node:vm';

test('panel presentation places Transit between Street Traffic and Bike Share in Movement', () => {
  const source = readFileSync(
    new URL('./layerPanel.js', import.meta.url),
    'utf8',
  );
  const declarations = source.slice(
    source.indexOf('const PANEL_GROUPS ='),
    source.indexOf('const PANEL_POSITIONS ='),
  );
  const order = JSON.parse(
    runInNewContext(`${declarations}\nJSON.stringify(PANEL_ORDER)`),
  );
  assert.deepEqual(
    order.filter(({ label }) => label === 'Movement').map(({ id }) => id),
    [
      'satellites',
      'flights',
      'military',
      'ais-live-vessels',
      'traffic',
      'transit',
      'bikeshare',
    ],
  );
  assert.equal(order.filter(({ id }) => id === 'transit').length, 1);
});

test('partial feed controls distinguish incomplete records from stale data and outages', async () => {
  const { LayerPanel, layerFeedState } = await import('./layerPanel.js');
  const classes = new Map();
  const attrs = new Map();
  const button = {
    classList: { toggle: (key, value) => classes.set(key, value) },
    dataset: {},
    setAttribute: (key, value) => attrs.set(key, value),
  };
  const layer = {
    id: 'ais-live-vessels',
    name: 'Live Vessels',
    source: 'AISStream',
    enabled: true,
    stats: {
      partial: true,
      stale: false,
      count: 2,
      acceptedRowCount: 2,
      rawRowCount: 3,
      lastUpdate: Date.now(),
    },
  };
  const panel = LayerPanel.prototype;
  panel._syncToggleButton(button, layer);
  assert.equal(button.textContent, 'PARTIAL');
  assert.equal(button.dataset.feedState, 'partial');
  assert.equal(classes.get('feed-partial'), true);
  assert.equal(classes.get('feed-stale'), false);
  assert.match(attrs.get('aria-label'), /PARTIAL/);
  assert.match(
    panel._buildMetaText(layer),
    /^PARTIAL · AISStream · 2 of 3 records accepted · /,
  );
  assert.match(
    panel._buildMetaText({
      ...layer,
      stats: { ...layer.stats, rawRowCount: 2 },
    }),
    /incomplete snapshot/,
  );
  assert.equal(layerFeedState({ ...layer.stats, stale: true }), 'stale');
  assert.equal(
    layerFeedState({ ...layer.stats, error: 'Connection lost' }),
    'degraded',
  );
  assert.equal(
    layerFeedState({ ...layer.stats, status: 'unavailable' }),
    'unavailable',
  );
  assert.equal(layerFeedState({ ...layer.stats, loading: true }), 'loading');
  layer.stats = { ...layer.stats, partial: false };
  panel._syncToggleButton(button, layer);
  assert.equal(button.textContent, 'ON');
  assert.equal(classes.get('feed-partial'), false);
  layer.enabled = false;
  panel._syncToggleButton(button, layer);
  assert.equal(button.textContent, 'OFF');
});

test('catalog filtering searches source metadata and selected state without toggling layers', async () => {
  const { LayerPanel } = await import('./layerPanel.js');
  const movement = { hidden: false },
    events = { hidden: false };
  const rows = new Map([
    [
      'flights',
      {
        row: { hidden: false },
        heading: movement,
        searchText: 'live flights opensky movement',
      },
    ],
    [
      'earthquakes',
      {
        row: { hidden: false },
        heading: events,
        searchText: 'earthquakes usgs events',
      },
    ],
  ]);
  const enabled = new Set(['earthquakes']);
  const panel = Object.assign(Object.create(LayerPanel.prototype), {
    _rows: rows,
    _query: 'usgs',
    _activeOnly: false,
    isEnabled: (id) => enabled.has(id),
    _summary: { textContent: '' },
    _empty: { hidden: true },
  });
  panel._applyFilters();
  assert.equal(rows.get('flights').row.hidden, true);
  assert.equal(movement.hidden, true);
  assert.equal(rows.get('earthquakes').row.hidden, false);
  assert.equal(panel._summary.textContent, '1 selected · 1 shown');
  panel._query = '';
  panel._activeOnly = true;
  panel._applyFilters();
  assert.equal(rows.get('flights').row.hidden, true);
  enabled.clear();
  panel._applyFilters();
  assert.equal(panel._empty.hidden, false);
  assert.equal(events.hidden, true);
  assert.equal(rows.size, 2, 'filtering retains row identity and focus owners');
});

test('unchanged legends retain nodes during status refresh', async () => {
  const { LayerPanel } = await import('./layerPanel.js');
  const legend = [
    { label: 'Slow', count: 3, color: '#d7b461', blurb: 'Measured road flow' },
  ];
  const node = {
    className: 'data-toggle-legend-item',
    dataset: {},
    remove() {
      assert.fail('unchanged legend was removed');
    },
  };
  const container = {
    children: [node],
    _legendSignature: JSON.stringify(
      legend.map(({ label, count, color, blurb }) => [
        label,
        count,
        color,
        blurb,
      ]),
    ),
    appendChild() {
      assert.fail('unchanged legend was recreated');
    },
  };
  const panel = Object.assign(Object.create(LayerPanel.prototype), {
    _rowControlsFor: () => ({ legend }),
  });
  panel._syncRowControls(container, { id: 'traffic', enabled: true });
  panel._syncRowControls(container, { id: 'traffic', enabled: true });
  assert.equal(container.children[0], node);
  assert.equal(container.hidden, false);
});
