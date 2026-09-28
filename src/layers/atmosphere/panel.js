/**
 * Atmosphere context-section presentation (pure — no DOM, no Cesium).
 *
 * `src/layers/atmosphere/index.js` owns the section node lifecycle (create on
 * enable, remove on disable); this module normalizes the latest proxy
 * snapshot into plain details and renders the section HTML, so every string
 * below is unit-testable in Node.
 */

/** Escape a value for interpolation into HTML text or attributes. */
export function escapeAtmosphereText(value) {
  return String(value ?? '').replace(
    /[&<>"']/g,
    (character) =>
      ({
        '&': '&amp;',
        '<': '&lt;',
        '>': '&gt;',
        '"': '&quot;',
        "'": '&#39;',
      })[character],
  );
}

const COMPASS_16 = [
  'N',
  'NNE',
  'NE',
  'ENE',
  'E',
  'ESE',
  'SE',
  'SSE',
  'S',
  'SSW',
  'SW',
  'WSW',
  'W',
  'WNW',
  'NW',
  'NNW',
];

/** Degrees → 16-point compass label, or null. Exported for tests. */
export function compassLabel(degrees) {
  if (!Number.isFinite(degrees)) return null;
  const normalized = ((degrees % 360) + 360) % 360;
  return COMPASS_16[Math.round(normalized / 22.5) % 16];
}

/** European AQI value → band label, or null. Exported for tests. */
export function europeanAqiBand(aqi) {
  if (!Number.isFinite(aqi)) return null;
  if (aqi < 0) return null;
  if (aqi < 20) return 'Good';
  if (aqi < 40) return 'Fair';
  if (aqi < 60) return 'Moderate';
  if (aqi < 80) return 'Poor';
  if (aqi < 100) return 'Very poor';
  return 'Extremely poor';
}

function formatPoint(lat, lon) {
  if (!Number.isFinite(lat) || !Number.isFinite(lon)) return null;
  const latText = `${Math.abs(lat).toFixed(4)}° ${lat >= 0 ? 'N' : 'S'}`;
  const lonText = `${Math.abs(lon).toFixed(4)}° ${lon >= 0 ? 'E' : 'W'}`;
  return `${latText}, ${lonText}`;
}

function formatUpdated(updatedAt) {
  if (!Number.isFinite(updatedAt)) return null;
  try {
    return new Date(updatedAt).toISOString().replace(/\.\d{3}Z$/, 'Z');
  } catch {
    return null;
  }
}

/**
 * Normalize one proxy snapshot into plain section details.
 * @param {object|null} snapshot Raw `/api/atmosphere` payload (or null).
 * @param {{updatedAt?: number|null, error?: string|null}} meta Layer state.
 */
export function atmosphereDetailsFromSnapshot(
  snapshot,
  { updatedAt = null, error = null } = {},
) {
  const item = snapshot?.item || null;
  const stale = snapshot?.stale === true;
  const windCompass = compassLabel(item?.windDirDeg);
  const aqiBand = europeanAqiBand(item?.europeanAqi);
  return {
    hasItem: Boolean(item),
    stale,
    error: typeof error === 'string' && error ? error : null,
    temperatureLabel: Number.isFinite(item?.temperatureC)
      ? `${item.temperatureC}°C`
      : null,
    feelsLikeLabel: Number.isFinite(item?.feelsLikeC)
      ? `${item.feelsLikeC}°C`
      : null,
    conditionLabel: item?.weatherLabel || null,
    humidityLabel: Number.isFinite(item?.humidityPct)
      ? `${item.humidityPct}%`
      : null,
    windLabel: Number.isFinite(item?.windKmh)
      ? `${item.windKmh} km/h${windCompass ? ` ${windCompass}` : ''}`
      : null,
    pressureLabel: Number.isFinite(item?.pressureHpa)
      ? `${item.pressureHpa} hPa`
      : null,
    todayLabel:
      Number.isFinite(item?.todayMaxC) || Number.isFinite(item?.todayMinC)
        ? `${Number.isFinite(item?.todayMinC) ? `${item.todayMinC}°` : '?'} / ${Number.isFinite(item?.todayMaxC) ? `${item.todayMaxC}°` : '?'}${Number.isFinite(item?.todayPrecipPct) ? ` · ${item.todayPrecipPct}%` : ''}`
        : null,
    tomorrowLabel:
      Number.isFinite(item?.tomorrowMaxC) || Number.isFinite(item?.tomorrowMinC)
        ? `${Number.isFinite(item?.tomorrowMinC) ? `${item.tomorrowMinC}°` : '?'} / ${Number.isFinite(item?.tomorrowMaxC) ? `${item.tomorrowMaxC}°` : '?'}${Number.isFinite(item?.tomorrowPrecipPct) ? ` · ${item.tomorrowPrecipPct}%` : ''}`
        : null,
    aqiLabel: Number.isFinite(item?.europeanAqi)
      ? `${item.europeanAqi}${aqiBand ? ` · ${aqiBand}` : ''}`
      : null,
    pmLabel:
      Number.isFinite(item?.pm25) || Number.isFinite(item?.pm10)
        ? `PM2.5 ${Number.isFinite(item?.pm25) ? item.pm25 : '?'} · PM10 ${Number.isFinite(item?.pm10) ? item.pm10 : '?'} µg/m³`
        : null,
    gasesLabel:
      Number.isFinite(item?.no2) || Number.isFinite(item?.ozone)
        ? `NO₂ ${Number.isFinite(item?.no2) ? item.no2 : '?'} · O₃ ${Number.isFinite(item?.ozone) ? item.ozone : '?'} µg/m³`
        : null,
    pointLabel: formatPoint(snapshot?.lat, snapshot?.lon),
    observedLabel: item?.observedAt || null,
    fetchedLabel: formatUpdated(updatedAt),
  };
}

/** Render the Context-panel section HTML for normalized details. */
export function atmosphereSectionHtml(details) {
  const esc = escapeAtmosphereText;
  if (!details) return '';
  if (details.error) {
    return (
      '<div class="context-atmosphere-view">' +
      '<div class="atmosphere-section-header"><span>ATMOSPHERE · FREE</span></div>' +
      `<p class="atmosphere-note">${esc(details.error)}</p>` +
      '</div>'
    );
  }
  if (!details.hasItem) {
    return (
      '<div class="context-atmosphere-view">' +
      '<div class="atmosphere-section-header"><span>ATMOSPHERE · FREE</span></div>' +
      '<p class="atmosphere-note">LOADING LATEST READOUT…</p>' +
      '</div>'
    );
  }
  const rows = [
    ['TEMP', details.temperatureLabel],
    ['FEELS LIKE', details.feelsLikeLabel],
    ['CONDITION', details.conditionLabel],
    ['HUMIDITY', details.humidityLabel],
    ['WIND', details.windLabel],
    ['PRESSURE', details.pressureLabel],
    ['TODAY MIN/MAX · RAIN%', details.todayLabel],
    ['TOMORROW MIN/MAX · RAIN%', details.tomorrowLabel],
    ['EUROPEAN AQI', details.aqiLabel],
    ['PARTICLES', details.pmLabel],
    ['GASES', details.gasesLabel],
    ['POINT', details.pointLabel],
    ['OBSERVED', details.observedLabel],
    ['FETCHED', details.fetchedLabel],
  ].filter(
    ([, value]) => value !== null && value !== undefined && value !== '',
  );
  const staleNote = details.stale
    ? '<p class="atmosphere-note">CACHED — upstream unreachable, showing last readout.</p>'
    : '';
  return (
    '<div class="context-atmosphere-view">' +
    `<div class="atmosphere-section-header"><span>ATMOSPHERE · FREE</span>${details.stale ? '<b>CACHED</b>' : ''}</div>` +
    '<dl class="atmosphere-details">' +
    rows
      .map(
        ([label, value]) =>
          `<div class="atmosphere-detail-row"><dt>${esc(label)}</dt><dd>${esc(value)}</dd></div>`,
      )
      .join('') +
    '</dl>' +
    staleNote +
    '<p class="atmosphere-note">Open-Meteo · CC BY 4.0 — model readout, not a station observation.</p>' +
    '</div>'
  );
}
