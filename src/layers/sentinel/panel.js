/**
 * Sentinel context-section presentation (pure — no DOM, no Cesium).
 *
 * `src/layers/sentinel/index.js` owns the section node lifecycle (create on
 * enable, remove on disable); this module normalizes the latest proxy
 * snapshot into plain details and renders the section HTML, so every string
 * below is unit-testable in Node.
 */

/** Escape a value for interpolation into HTML text or attributes. */
export function escapeSentinelText(value) {
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

/** Human layer-kind label; S1 is radar, never a photo. */
export function sentinelKindLabel(kind) {
  return kind === 's1'
    ? 'SENTINEL-1 SAR · RADAR, NOT PHOTO'
    : 'SENTINEL-2 OPTICAL · 10M';
}

function formatCoordinate(value, positive, negative) {
  if (!Number.isFinite(value)) return null;
  return `${Math.abs(value).toFixed(4)}° ${value >= 0 ? positive : negative}`;
}

function formatPoint(lat, lon) {
  const latText = formatCoordinate(lat, 'N', 'S');
  const lonText = formatCoordinate(lon, 'E', 'W');
  return latText && lonText ? `${latText}, ${lonText}` : null;
}

function formatBbox(bbox) {
  if (!Array.isArray(bbox) || bbox.length !== 4) return null;
  const [west, south, east, north] = bbox;
  if (![west, south, east, north].every(Number.isFinite)) return null;
  return `${west.toFixed(2)}, ${south.toFixed(2)} → ${east.toFixed(2)}, ${north.toFixed(2)}`;
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
 * @param {object|null} snapshot Raw `/api/sentinel*-latest` payload (or null).
 * @param {'s1'|'s2'} kind Requested layer kind.
 * @param {{updatedAt?: number|null, error?: string|null}} meta Layer state.
 */
export function sentinelDetailsFromSnapshot(
  snapshot,
  kind,
  { updatedAt = null, error = null } = {},
) {
  const item = snapshot?.item || null;
  const fallback =
    snapshot?.fallback === true ? snapshot : snapshot?.fallback || null;
  const isSar = (snapshot?.kind || kind) === 's1';
  const cloudCover =
    item && Number.isFinite(item.cloudCover) ? item.cloudCover : null;
  return {
    kind: isSar ? 's1' : 's2',
    kindLabel: sentinelKindLabel(isSar ? 's1' : 's2'),
    hasItem: Boolean(item),
    name: item?.name || null,
    collection: item?.collection || null,
    datetime: item?.datetime || null,
    dateLabel: item?.datetime ? String(item.datetime).slice(0, 10) : null,
    cloudCover,
    cloudLabel: isSar
      ? 'N/A — RADAR'
      : cloudCover === null
        ? 'N/A'
        : `${cloudCover}%`,
    pointLabel: formatPoint(snapshot?.lat, snapshot?.lon),
    bboxLabel: formatBbox(item?.bbox),
    browserUrl: item?.browserUrl || fallback?.browserUrl || null,
    eoBrowserUrl: item?.eoBrowserUrl || fallback?.eoBrowserUrl || null,
    fallbackNote:
      fallback && !item
        ? fallback.note ||
          fallback.statusMessage ||
          'Catalogue unreachable — open Copernicus Browser link-out.'
        : null,
    stale: snapshot?.stale === true,
    fetchedLabel: formatUpdated(snapshot?.fetchedAt),
    updatedLabel: formatUpdated(updatedAt),
    error: error || null,
  };
}

function sentinelDetailRow(label, value) {
  if (value === null || value === undefined || String(value).trim() === '')
    return '';
  const text = escapeSentinelText(value);
  return `<div class="sentinel-detail-row"><dt>${escapeSentinelText(label)}</dt><dd title="${text}">${text}</dd></div>`;
}

function sentinelLinks(browserUrl, eoBrowserUrl) {
  const links = [];
  if (browserUrl)
    links.push(
      `<a class="sentinel-link" href="${escapeSentinelText(browserUrl)}" target="_blank" rel="noopener noreferrer">COPERNICUS BROWSER</a>`,
    );
  if (eoBrowserUrl)
    links.push(
      `<a class="sentinel-link" href="${escapeSentinelText(eoBrowserUrl)}" target="_blank" rel="noopener noreferrer">EO BROWSER</a>`,
    );
  if (!links.length) return '';
  return `<div class="sentinel-links">${links.join('')}</div>`;
}

/**
 * Render the context-section body for normalized details.
 * Covers loading (no snapshot yet), error, fallback and full-item states.
 */
export function sentinelSectionHtml(details) {
  const header = `<div class="sentinel-section-header"><span>SENTINEL LATEST-CLEAR</span><b>${escapeSentinelText(details.kindLabel)}</b></div>`;
  if (details.error && !details.hasItem) {
    return (
      `${header}<p class="sentinel-note">SOURCE ERROR — ${escapeSentinelText(details.error)}</p>` +
      sentinelLinks(details.browserUrl, details.eoBrowserUrl)
    );
  }
  if (!details.hasItem) {
    if (details.fallbackNote || details.browserUrl || details.eoBrowserUrl) {
      return (
        `${header}<p class="sentinel-note">${escapeSentinelText(details.fallbackNote || 'No scene available — try the browser link-outs.')}</p>` +
        sentinelLinks(details.browserUrl, details.eoBrowserUrl)
      );
    }
    return `${header}<p class="sentinel-note">LOADING LATEST SCENE…</p>`;
  }
  const stale = details.stale
    ? '<p class="sentinel-note">CACHED — CATALOGUE UNREACHABLE ON LAST REFRESH</p>'
    : '';
  return (
    `${header}<dl class="sentinel-details">` +
    sentinelDetailRow('PRODUCT', details.name) +
    sentinelDetailRow('COLLECTION', details.collection) +
    sentinelDetailRow('ACQUIRED', details.datetime) +
    sentinelDetailRow('CLOUD', details.cloudLabel) +
    sentinelDetailRow('POINT', details.pointLabel) +
    sentinelDetailRow('FOOTPRINT W,S,E,N', details.bboxLabel) +
    sentinelDetailRow('FETCHED', details.fetchedLabel) +
    `</dl>${stale}` +
    sentinelLinks(details.browserUrl, details.eoBrowserUrl)
  );
}
