"""Site-scale change review of GodEye's calibrated Sigma0 products.

This measures a radar signal, not construction, damage, or object identities.
Complex calibration remains in SNAP. A bounded AOI is read using Rasterio
windows; the later linear-power image is bilinearly aligned to the earlier grid.
Local means are formed in LINEAR power before taking the log ratio. This
explicit spatial averaging is not advertised as a Lee filter or added resolution.
"""
import contextlib
import datetime as dt
import hashlib
import json
import math
import os
from pathlib import Path
import signal
import time
import traceback
import uuid

import numpy as np
import rasterio
from rasterio.enums import Resampling
from rasterio.features import shapes, sieve
from rasterio.warp import reproject
from rasterio.windows import Window, from_bounds
from PIL import Image

MAX_PIXELS = 1_000_000
MAX_CANDIDATES = 500
NODATA = -9999.0


class ReviewError(Exception):
    pass


def read_json(path):
    if path.stat().st_size > 1024 * 1024:
        raise ReviewError('The saved acquisition metadata exceeds the size limit.')
    return json.loads(path.read_text(encoding='utf-8'))


def source_job_id(directory):
    """Keep source UUIDs for both legacy jobs and the portable owned-job layout."""
    try:
        return str(uuid.UUID(directory.name))
    except ValueError:
        if directory.name == 'product':
            try:
                identity = str(uuid.UUID(directory.parent.name))
                record = read_json(directory.parent/'run.json')
                if record.get('version') == 1 and str(uuid.UUID(record.get('id', ''))) == identity:
                    return identity
            except (OSError, ValueError, TypeError):
                pass
        raise ReviewError('A source acquisition lost its saved job UUID. Import the complete original job folder.') from None


def timestamp(value):
    # SAFE timestamps sometimes omit the UTC suffix. Sentinel-1 times are UTC.
    date = dt.datetime.fromisoformat(value.replace('Z', '+00:00'))
    return date.replace(tzinfo=dt.timezone.utc) if date.tzinfo is None else date


def admit_pair(before, after, polarization):
    for record in (before, after):
        if record.get('version') != 1 or record.get('state') != 'completed':
            raise ReviewError('Choose two completed GodEye SAR processing results.')
        if record.get('metadata', {}).get('productType') != 'GRD':
            raise ReviewError('Only calibrated Sentinel-1 GRD results are supported.')
        stages = record.get('processingStages', [])
        if 'Calibration' not in stages or 'Terrain-Correction' not in stages:
            raise ReviewError('Both acquisitions must have recorded Sigma0 calibration and terrain correction.')
    a, b = before['metadata'], after['metadata']
    if a.get('orbitDirection') not in ('ASCENDING', 'DESCENDING') or not isinstance(a.get('relativeOrbit'), int) or not 1 <= a['relativeOrbit'] <= 175:
        raise ReviewError('Both acquisitions need a known Sentinel-1 pass direction and relative orbit (1–175).')
    try:
        if timestamp(a['acquisitionStart']) >= timestamp(b['acquisitionStart']):
            raise ReviewError('The later acquisition must be newer than the baseline.')
    except (KeyError, ValueError, TypeError):
        raise ReviewError('Acquisition timestamps are missing or invalid.') from None
    for key in ('mission', 'satellite', 'mode', 'orbitDirection', 'relativeOrbit'):
        if a.get(key) in (None, '', 'UNKNOWN') or a.get(key) != b.get(key):
            raise ReviewError('Use the same satellite, acquisition mode, direction and known relative orbit. Mismatched geometry can resemble infrastructure change.')
    for key in ('applyOrbit', 'speckle', 'pixelSpacing'):
        if key not in before.get('options', {}) or before['options'].get(key) != after.get('options', {}).get(key):
            raise ReviewError('Process both acquisitions with matching orbit, speckle and output-spacing settings.')
    if polarization not in ('VV', 'VH'):
        raise ReviewError('Select VV or VH, not the false-color composite.')
    outputs = []
    for record in (before, after):
        output = next((o for o in record.get('outputs', []) if o['id'] == polarization), None)
        if output is None or not output.get('scientific'):
            raise ReviewError('Both acquisitions need a calibrated floating-point output for the selected polarization.')
        outputs.append(output)
    return outputs


def local_power(values, size):
    """Valid-only box mean with >=80% valid support; invalid centers stay invalid."""
    valid = np.isfinite(values) & (values > 0)
    pad = size // 2
    def sums(array):
        integral = np.pad(array, pad).cumsum(0, dtype=np.float64).cumsum(1, dtype=np.float64)
        integral = np.pad(integral, ((1, 0), (1, 0)))
        return integral[size:, size:] - integral[:-size, size:] - integral[size:, :-size] + integral[:-size, :-size]
    counts = sums(valid.astype(np.uint8))
    total = sums(np.where(valid, values, 0))
    usable = valid & (counts >= math.ceil(size * size * .8))
    return np.divide(total, counts, out=np.full(values.shape, np.nan), where=usable)


def delta_db(before, after, size):
    a, b = local_power(before, size), local_power(after, size)
    valid = np.isfinite(a) & np.isfinite(b) & (a > 0) & (b > 0)
    delta = np.full(a.shape, np.nan, dtype=np.float32)
    delta[valid] = 10 * (np.log10(b[valid]) - np.log10(a[valid]))
    return a, b, delta


def safe_file(directory, name):
    if not isinstance(name, str) or Path(name).name != name or any(c in name for c in ('/', '\\', ':')):
        raise ReviewError('The saved raster path is invalid.')
    file = directory / name
    if file.resolve().parent != directory.resolve() or not file.is_file():
        raise ReviewError('A calibrated raster is missing or is outside its saved job.')
    return file


def crop_window(dataset, bounds):
    raw = from_bounds(*bounds, transform=dataset.transform)
    epsilon = 1e-7  # Numerical roundoff in geographic transforms, in pixels.
    window = Window(math.floor(raw.col_off+epsilon), math.floor(raw.row_off+epsilon),
                    math.ceil(raw.col_off + raw.width-epsilon) - math.floor(raw.col_off+epsilon),
                    math.ceil(raw.row_off + raw.height-epsilon) - math.floor(raw.row_off+epsilon))
    if window.col_off < 0 or window.row_off < 0 or window.col_off + window.width > dataset.width or window.row_off + window.height > dataset.height:
        raise ReviewError('The site must be inside both processed raster extents. Choose a smaller site or a covering acquisition.')
    if window.width * window.height > MAX_PIXELS:
        raise ReviewError('Choose a smaller site: change review is limited to one million source-grid pixels per review.')
    if min(window.width, window.height) < 9:
        raise ReviewError('Choose a site at least nine pixels across for a meaningful regional comparison.')
    return window


def read_power(dataset, window):
    array = dataset.read(1, window=window, masked=True).astype(np.float32).filled(np.nan)
    array[~np.isfinite(array) | (array <= 0)] = np.nan
    return array


def validate_grid(dataset):
    t = dataset.transform
    if dataset.crs != rasterio.crs.CRS.from_epsg(4326) or dataset.count != 1 or dataset.dtypes[0] not in ('float32', 'float64') or t.a <= 0 or t.e >= 0 or t.b != 0 or t.d != 0:
        raise ReviewError('Expected a north-up EPSG:4326 floating-point GodEye Sigma0 raster.')
    quantity = dataset.tags().get('quantity', '')
    # GodEye records provenance in a JSON tag; the stage admission also checks
    # immutable processing metadata. Never accept a dB/8-bit preview as power.
    if quantity != 'Sigma0' or dataset.tags().get('units') != 'linear power':
        raise ReviewError('Comparison requires linear Sigma0 power, not logarithmic or rendered pixels.')


def save_preview(array, path):
    image = Image.fromarray(array)
    image.thumbnail((2048, 2048), Image.Resampling.NEAREST)
    image.save(path)


def candidates_for(delta, transform, threshold, minimum, save_mask):
    valid = np.isfinite(delta)
    raw = np.zeros(delta.shape, dtype=np.uint8)
    raw[valid & (delta >= threshold)] = 1
    raw[valid & (delta <= -threshold)] = 2
    # Connected same-sign patches, not object boxes or machine probabilities.
    mask = sieve(raw, size=minimum, connectivity=8)
    mask[raw != mask] = 0
    save_mask(mask)
    candidates = []
    earth_radius = 6371008.8
    rows = np.arange(delta.shape[0])
    latitudes = transform.f + (rows + .5) * transform.e
    pixel_areas = (earth_radius ** 2 * abs(math.radians(transform.a) * math.radians(transform.e)) * np.cos(np.radians(latitudes)))
    # Polygonize once on a bounded AOI; no whole-scene raster read. Stats use a
    # small local mask so no full-sized mask is allocated for every candidate.
    from rasterio.features import geometry_mask
    for geometry, value in shapes(mask, mask=mask > 0, transform=transform, connectivity=8):
        ring = geometry['coordinates'][0]
        xs, ys = zip(*ring)
        bounds = [min(xs), min(ys), max(xs), max(ys)]
        w = from_bounds(*bounds, transform=transform).round_offsets().round_lengths()
        c, r, width, height = int(w.col_off), int(w.row_off), int(w.width), int(w.height)
        inside = geometry_mask([geometry], (height, width), rasterio.windows.transform(w, transform), invert=True)
        local = delta[r:r+height, c:c+width]
        inside &= np.isfinite(local) & (mask[r:r+height, c:c+width] == int(value))
        count = int(inside.sum())
        if count < minimum:
            continue
        candidates.append(dict(id=str(len(candidates)+1), direction='increase' if value == 1 else 'decrease',
                               bounds=bounds, pixels=count, approximateAreaM2=float((inside * pixel_areas[r:r+height, None]).sum()),
                               meanDeltaDb=float(local[inside].mean()), maximumAbsoluteDeltaDb=float(np.abs(local[inside]).max())))
        if len(candidates) > MAX_CANDIDATES:
            raise ReviewError('More than 500 candidate patches. Increase the minimum patch size or threshold, or choose a smaller site.')
    return sorted(candidates, key=lambda c: c['approximateAreaM2'], reverse=True)


def analyze(request, destination, emit, save_json):
    site = request['site']
    region = site['region']
    bounds = [float(region[k]) for k in ('west', 'south', 'east', 'north')]
    options = request['options']
    pol, threshold, size, minimum = options['polarization'], float(options['thresholdDb']), int(options['averagingWindow']), int(options['minimumPixels'])
    if not all(math.isfinite(x) for x in bounds) or not (-180 <= bounds[0] < bounds[2] <= 180 and -85 <= bounds[1] < bounds[3] <= 85) or max(bounds[2]-bounds[0], bounds[3]-bounds[1]) > 10:
        raise ReviewError('The site boundary is invalid.')
    if not math.isfinite(threshold) or not 1 <= threshold <= 20 or size not in (3, 5, 7) or not 9 <= minimum <= 10000:
        raise ReviewError('Use a 1–20 dB threshold, a 3/5/7 pixel averaging window and at least nine connected pixels.')
    directories = [Path(request[k]) for k in ('beforeDirectory', 'afterDirectory')]
    records = [read_json(d/'result.json') for d in directories]
    outputs = admit_pair(*records, pol)
    if request.get('sourceHashes') is not None:
        actual = [hashlib.sha256((d/'result.json').read_bytes()).hexdigest() for d in directories]
        if actual != request['sourceHashes']:
            raise ReviewError('A source manifest changed after the monitoring run was queued. Reimport and schedule again.')
    evidence = []
    quality = None
    for directory, record, output in zip(directories, records, outputs):
        evidence.append(dict(jobID=source_job_id(directory), product=record['inputName'], metadata=record['metadata'], options=record['options'],
                             metadataSHA256=hashlib.sha256((directory/'result.json').read_bytes()).hexdigest(), scientific=output['scientific'],
                             engine=record.get('engine', {}), processingStages=record.get('processingStages', []), output=output))
    files = [safe_file(d, o['scientific']) for d, o in zip(directories, outputs)]
    if request.get('sourceFileIdentities') is not None:
        actual = [dict(bytes=f.stat().st_size, modifiedMilliseconds=f.stat().st_mtime_ns//1_000_000) for f in files]
        if actual != request['sourceFileIdentities']:
            raise ReviewError('A scientific source file changed after the monitoring run was queued. Reimport and schedule again.')
    emit('processing', 'Aligning calibrated acquisitions to the baseline grid', .15)
    with contextlib.ExitStack() as stack:
        datasets = [stack.enter_context(rasterio.open(f)) for f in files]
        for dataset in datasets:
            validate_grid(dataset)
            crop_window(dataset, bounds)
        a, source_b = datasets
        if abs(a.transform.a-source_b.transform.a)/a.transform.a > .001 or abs(a.transform.e-source_b.transform.e)/abs(a.transform.e) > .001:
            raise ReviewError('The processed pixel grids have different spacing. Reprocess with matching output spacing.')
        window = crop_window(a, bounds)
        before = read_power(a, window)
        if request.get('registration') is not None:
            import importlib.util
            spec = importlib.util.spec_from_file_location('godeye_registration', Path(__file__).with_name('registration.py'))
            registration = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(registration)
            emit('processing', 'Estimating translation and checking independent stable controls', .18)
            translation, quality = registration.register(a, source_b, request['registration'])
            after = registration.aligned_power(source_b, before.shape, a.window_transform(window), translation)
            with rasterio.open(destination/'corrected-linear-power.tif', 'w', driver='GTiff',
                               width=int(window.width), height=int(window.height), count=1, dtype='float32',
                               crs=a.crs, transform=a.window_transform(window), nodata=NODATA, compress='deflate') as corrected:
                corrected.write(np.where(np.isfinite(after), after, NODATA).astype('float32'), 1)
                corrected.update_tags(quantity='Sigma0', units='linear power', derivation='Translation-verified sampling from original later raster',
                                      registration=json.dumps(quality, allow_nan=False))
        else:
            source_window = crop_window(source_b, list(rasterio.windows.bounds(window, a.transform)))
            c, r = max(0, int(source_window.col_off)-2), max(0, int(source_window.row_off)-2)
            ec, er = min(source_b.width, int(source_window.col_off+source_window.width)+2), min(source_b.height, int(source_window.row_off+source_window.height)+2)
            source_window = Window(c, r, ec-c, er-r)
            after = np.full(before.shape, np.nan, dtype=np.float32)
            reproject(read_power(source_b, source_window), after,
                      src_transform=source_b.window_transform(source_window), src_crs=source_b.crs, src_nodata=np.nan,
                      dst_transform=a.window_transform(window), dst_crs=a.crs, dst_nodata=np.nan,
                      resampling=Resampling.bilinear, warp_mem_limit=64, num_threads=1)
        for item, power, file in zip(evidence, (before, after), files):
            item['alignedSitePowerSHA256'] = hashlib.sha256(power.tobytes(order='C')).hexdigest()
            item['scientificBytes'] = file.stat().st_size
        # Match validity BEFORE averaging so each local mean uses the same support.
        common = np.isfinite(before) & np.isfinite(after)
        before[~common] = np.nan
        after[~common] = np.nan
        emit('processing', 'Measuring local linear-power log ratios', .35)
        mean_a, mean_b, delta = delta_db(before, after, size)
        valid = np.isfinite(delta)
        coverage = float(valid.mean())
        if coverage < .5:
            if quality is not None:
                raise registration.QualityBlocked('Common valid coverage is below 50% after correction and averaging.', {**quality, 'commonValidFraction': coverage})
            raise ReviewError('Less than 50% of this site has common valid radar coverage after averaging. Choose a better-covered site or another acquisition pair.')
        transform = a.window_transform(window)
        profile = dict(driver='GTiff', width=int(window.width), height=int(window.height), crs=a.crs, transform=transform,
                       tiled=True, blockxsize=256, blockysize=256, compress='deflate', BIGTIFF='IF_SAFER')
        with rasterio.open(destination/'delta-db.tif', 'w', **profile, dtype='float32', count=1, nodata=NODATA) as target:
            target.write(np.where(valid, delta, NODATA).astype('float32'), 1)
            target.update_tags(units='dB', quantity='10*log10(after local mean Sigma0 / before local mean Sigma0)',
                               acquisitionBefore=records[0]['metadata']['acquisitionStart'], acquisitionAfter=records[1]['metadata']['acquisitionStart'],
                               averagingWindow=str(size), resampling='later linear power: bilinear to baseline grid')
            target.update_tags(processing=json.dumps(dict(site=site, options=options, evidence=evidence), allow_nan=False))
        emit('processing', 'Finding connected same-sign change candidates', .55)
        def save_mask(mask):
            with rasterio.open(destination/'candidate-mask.tif', 'w', **profile, dtype='uint8', count=1, nodata=0) as target:
                target.write(mask, 1)
                target.update_tags(classes='0=not a candidate or invalid; 1=increase; 2=decrease')
        candidates = candidates_for(delta, transform, threshold, minimum, save_mask)
        emit('exporting', 'Rendering shared-scale before/after and change overlays', .75)
        values = np.concatenate([10*np.log10(mean_a[valid]), 10*np.log10(mean_b[valid])])
        low, high = np.percentile(values, [2, 98])
        high = max(float(high), float(low)+1)
        for name, power in (('Before', mean_a), ('After', mean_b)):
            gray = np.zeros(delta.shape, dtype=np.uint8)
            gray[valid] = np.rint(np.clip((10*np.log10(power[valid])-low)/(high-low), 0, 1)*255).astype(np.uint8)
            rgba = np.stack([gray, gray, gray, valid.astype(np.uint8)*255], axis=-1)
            save_preview(rgba, destination/(name+'.png'))
        # Orange increase / blue decrease / neutral gray; fixed symmetric scale.
        # Saturation at 2*threshold is display clipping, not a likelihood score.
        strength = np.clip(np.nan_to_num(np.abs(delta), nan=0)/(threshold*2), 0, 1)[..., None]
        base = np.full((*delta.shape, 3), 100.0)
        color = np.where((delta >= 0)[..., None], np.array([235, 140, 45]), np.array([45, 150, 230]))
        rgb = np.rint(base*(1-strength)+color*strength).astype(np.uint8)
        rgba = np.dstack([rgb, valid.astype(np.uint8)*255])
        save_preview(rgba, destination/'Change.png')
        with rasterio.open(destination/'change-display.tif', 'w', **profile, dtype='uint8', count=4) as target:
            target.write(np.moveaxis(rgba, -1, 0))
            target.colorinterp = (rasterio.enums.ColorInterp.red, rasterio.enums.ColorInterp.green, rasterio.enums.ColorInterp.blue, rasterio.enums.ColorInterp.alpha)
        actual_bounds = list(rasterio.windows.bounds(window, a.transform))
    return dict(analysisStatus='ready', quality=quality, site=site, options=options, evidence=evidence, candidates=candidates, bounds=actual_bounds,
                width=int(window.width), height=int(window.height), commonValidFraction=coverage, sharedDisplayRangeDb=[float(low), high],
                changeDisplayRangeDb=[-2*threshold, 2*threshold], crs='EPSG:4326',
                alignment=('Independent-control verified relative translation; absolute geolocation remains unknown.' if quality else 'Bilinear resampling of later linear power to baseline grid; geocoding is inherited, not independently validated.'),
                quantity='Local mean Sigma0 log ratio (after minus before in dB)',
                limitations=['Candidate patches are radar changes, not confirmed infrastructure events.',
                             'Moisture, vegetation, season, speckle and SAR viewing effects can cause changes.',
                             'Same-satellite/orbit checks do not verify sub-pixel co-registration or absolute radiometric accuracy.',
                             'Sigma0 is not radiometrically terrain-flattened; steep-terrain changes need extra scrutiny.'])


def run(request_file, destination, emit, save_json):
    destination = Path(destination)
    if destination.exists():
        raise ReviewError('Review destination already exists.')
    destination.mkdir(parents=True, mode=0o700)
    result = dict(version=1, workerPID=os.getpid(), createdAt=dt.datetime.now(dt.timezone.utc).isoformat(), state='queued')
    started = time.monotonic()
    previous = signal.signal(signal.SIGTERM, lambda *_: (_ for _ in ()).throw(ReviewError('Review cancelled.')))
    save_json(destination/'result.json', result)
    try:
        emit('validating', 'Checking dates, geometry, processing settings and site coverage', .02)
        request = read_json(Path(request_file))
        result.update(site=request.get('site'), options=request.get('options'),
                      sourceJobIDs=[source_job_id(Path(request.get(k, ''))) for k in ('beforeDirectory', 'afterDirectory')])
        save_json(destination/'result.json', result)
        result['sourceMetadataSHA256'] = [hashlib.sha256((Path(request[k])/'result.json').read_bytes()).hexdigest() for k in ('beforeDirectory', 'afterDirectory')]
        result.update(analyze(request, destination, emit, save_json))
        if result.get('quality'): save_json(destination/'quality.json', result['quality'])
        result.update(state='completed', completedAt=dt.datetime.now(dt.timezone.utc).isoformat(), elapsedSeconds=time.monotonic()-started)
        save_json(destination/'result.json', result)
        emit('completed', 'Infrastructure change review ready', 1)
    except BaseException as error:
        if isinstance(getattr(error, 'report', None), dict):
            emit('exporting', 'Retaining blocked registration quality report', .95)
            result.update(state='completed', analysisStatus='blocked', quality=error.report, candidates=[],
                          completedAt=dt.datetime.now(dt.timezone.utc).isoformat(), elapsedSeconds=time.monotonic()-started)
            save_json(destination/'quality.json', error.report)
            save_json(destination/'result.json', result)
            emit('completed', 'Analysis blocked by registration quality checks', 1)
            return
        (destination/'diagnostics.log').write_text(traceback.format_exc()[-16000:], encoding='utf-8')
        for file in destination.iterdir():
            if file.name not in ('result.json', 'diagnostics.log', 'events.jsonl'):
                file.unlink()
        result.update(state='failed', error=str(error) if isinstance(error, ReviewError) else 'Change review failed. A local diagnostic log is available.', elapsedSeconds=time.monotonic()-started)
        save_json(destination/'result.json', result)
        emit('failed', result['error'])
        raise
    finally:
        signal.signal(signal.SIGTERM, previous)
