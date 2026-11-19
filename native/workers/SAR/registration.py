"""Bounded relative translation checks. Never an absolute geolocation certificate."""
import math
import uuid
import numpy as np
import rasterio
from rasterio import Affine
from rasterio.enums import Resampling
from rasterio.warp import reproject
from rasterio.windows import Window, from_bounds


class QualityBlocked(Exception):
    def __init__(self, message, report):
        super().__init__(message)
        self.report = {**report, 'state': 'blocked', 'reason': message}


def compose(a,b):
    return a @ b if hasattr(a, "__matmul__") else a*b


def aligned_power(dataset, shape, transform, translation=(0, 0)):
    """One bilinear sampling from ORIGINAL linear power, with explicit nodata."""
    corrected = compose(dataset.transform, Affine.translation(translation[1], translation[0]))
    bounds = rasterio.transform.array_bounds(*shape, transform)
    raw = from_bounds(*bounds, transform=corrected)
    c, r = max(0, math.floor(raw.col_off)-2), max(0, math.floor(raw.row_off)-2)
    ec, er = min(dataset.width, math.ceil(raw.col_off+raw.width)+2), min(dataset.height, math.ceil(raw.row_off+raw.height)+2)
    target = np.full(shape, np.nan, dtype='float32')
    if ec <= c or er <= r:
        return target
    if (ec-c)*(er-r) > 1_100_000:
        raise ValueError('Aligned source window exceeds the bounded analysis limit.')
    values = dataset.read(1, window=Window(c, r, ec-c, er-r), masked=True).astype('float32').filled(np.nan)
    values[~np.isfinite(values) | (values <= 0)] = np.nan
    reproject(values, target, src_transform=compose(corrected, Affine.translation(c, r)), src_crs=dataset.crs,
              src_nodata=np.nan, dst_transform=transform, dst_crs=dataset.crs, dst_nodata=np.nan,
              resampling=Resampling.bilinear, num_threads=1, warp_mem_limit=32)
    return target


def check_configuration(config):
    controls = config.get('controls', [])
    if config.get('version') != 1 or not 6 <= len(controls) <= 12:
        raise ValueError('Configure 6–12 distinct stable controls, including three Fit and three Verify controls.')
    ids, rectangles = set(), []
    for c in controls:
        identity = str(uuid.UUID(c['id']))
        b = c['region']
        rect = [float(b[k]) for k in ('west', 'south', 'east', 'north')]
        if identity in ids or c.get('role') not in ('Fit', 'Verify') or not all(map(math.isfinite, rect)) or not (-180 <= rect[0] < rect[2] <= 180 and -85 <= rect[1] < rect[3] <= 85) or max(rect[2]-rect[0], rect[3]-rect[1]) > 10:
            raise ValueError('Stable control identity, role or geometry is invalid.')
        if any(rect[0] < b[2] and rect[2] > b[0] and rect[1] < b[3] and rect[3] > b[1] for b in rectangles):
            raise ValueError('Fit and verification controls must be spatially independent and non-overlapping.')
        ids.add(identity); rectangles.append(rect)
    if any(sum(c['role'] == role for c in controls) < 3 for role in ('Fit', 'Verify')):
        raise ValueError('At least three Fit and three independent Verify controls are required.')
    for key, default, low, high in [('maximumShiftPixels', 2, .5, 8), ('maximumResidualPixels', .5, .1, 1), ('minimumCorrelation', .6, .5, .99)]:
        value = config.get(key, default)
        if not isinstance(value, (float, int)) or isinstance(value, bool) or not math.isfinite(value) or not low <= value <= high:
            raise ValueError('Registration policy is outside its supported range.')


def estimate(reference, moving, maximum, correlation):
    from scipy.ndimage import gaussian_filter, shift
    from skimage.registration import phase_cross_correlation
    if min(reference.shape) < 64 or not np.all(np.isfinite(reference) & np.isfinite(moving) & (reference > 0) & (moving > 0)):
        raise ValueError('Control needs at least 64 × 64 common valid pixels; nodata is not filled.')
    a, b = [gaussian_filter(10*np.log10(v.astype('float64')), 1) for v in (reference, moving)]
    if min(a.std(), b.std()) < .1:
        raise ValueError('Control has insufficient radar texture.')
    # Rectangular fully valid chips permit the documented subpixel estimator;
    # its masked mode does not provide the same upsampled estimate.
    window = np.outer(np.hanning(a.shape[0]), np.hanning(a.shape[1]))
    offset, _, _ = phase_cross_correlation((a-a.mean())*window, (b-b.mean())*window,
                                          upsample_factor=20, normalization=None)
    reverse, _, _ = phase_cross_correlation((b-b.mean())*window, (a-a.mean())*window,
                                           upsample_factor=20, normalization=None)
    if not np.all(np.isfinite(offset)) or np.linalg.norm(offset) > maximum or np.linalg.norm(offset+reverse) > .15:
        raise ValueError('Control shift is excessive, ambiguous or not reversible.')
    aa, bb = (a-a.mean())*window, (b-b.mean())*window
    surface = np.fft.ifft2(np.fft.fft2(aa)*np.conj(np.fft.fft2(bb))).real
    peak_row, peak_col = np.unravel_index(np.argmax(surface), surface.shape)
    peak = float(surface[peak_row, peak_col])
    radius = max(4, math.ceil(2*maximum))
    for dy in range(-radius, radius+1):
        for dx in range(-radius, radius+1):
            if math.hypot(dy, dx) < 2.5: continue
            if peak > 0 and surface[(peak_row+dy) % surface.shape[0], (peak_col+dx) % surface.shape[1]] > .95*peak:
                raise ValueError('Control has competing correlation peaks; translation is ambiguous.')
    corrected = shift(b, offset, order=1, mode='constant', cval=np.nan, prefilter=False)
    margin = math.ceil(maximum)+3
    aa, bb = a[margin:-margin, margin:-margin], corrected[margin:-margin, margin:-margin]
    ncc = float(np.corrcoef(aa.ravel(), bb.ravel())[0, 1])
    if not math.isfinite(ncc) or ncc < correlation:
        raise ValueError('Control correlation is below the configured minimum.')
    return np.asarray(offset), ncc


def register(before, after, config):
    report = dict(version=1, state='checking', method='Translation-only; windowed smoothed log-intensity phase cross-correlation',
                  absoluteGeolocation='unknown', absoluteRadiometry='unknown', controls=[], policy=config,
                  limitations=['Relative registration checks do not establish absolute geolocation or radiometric accuracy.',
                               'Stable controls are analyst assertions. Weather, season and changing controls can invalidate them.',
                               'Residual thresholds are provisional engineering settings, not certified accuracy.'])
    try:
        check_configuration(config)
        import scipy
        import skimage
        report['versions'] = dict(numpy=np.__version__, rasterio=rasterio.__version__, scipy=scipy.__version__, skimage=skimage.__version__)
    except (ValueError, KeyError, TypeError, ImportError) as e:
        raise QualityBlocked(str(e) if not isinstance(e, ImportError) else 'Install requirements-monitoring.txt in the selected raster Python environment.', report) from e
    maximum, residual, correlation = [float(config.get(k, d)) for k, d in [('maximumShiftPixels', 2), ('maximumResidualPixels', .5), ('minimumCorrelation', .6)]]
    chips, fits = [], []
    for control in config['controls']:
        item = dict(id=control['id'], name=control.get('name', ''), role=control['role'], region=control['region'])
        report['controls'].append(item)
        try:
            bounds = [control['region'][k] for k in ('west', 'south', 'east', 'north')]
            raw = from_bounds(*bounds, transform=before.transform)
            width, height = min(256, math.floor(raw.width)), min(256, math.floor(raw.height))
            window = Window(math.floor(raw.col_off+(raw.width-width)/2), math.floor(raw.row_off+(raw.height-height)/2), width, height)
            if min(width, height) < 64 or window.col_off < 0 or window.row_off < 0 or window.col_off+width > before.width or window.row_off+height > before.height:
                raise ValueError('Control does not cover a usable 64–256 pixel chip inside the reference.')
            transform = before.window_transform(window)
            a = before.read(1, window=window, masked=True).astype('float32').filled(np.nan)
            b = aligned_power(after, a.shape, transform)
            offset, ncc = estimate(a, b, maximum, correlation)
            item.update(state='usable', beforeShiftPixels=offset.tolist(), correlation=ncc,
                        sampledBounds=list(rasterio.windows.bounds(window, before.transform)))
            chips.append((control, item, window, offset))
            if control['role'] == 'Fit': fits.append(offset)
        except (ValueError, TypeError) as e:
            item.update(state='unavailable', reason=str(e))
    if len(fits) < 3 or sum(c['role'] == 'Verify' for c, *_ in chips) < 3:
        raise QualityBlocked('Fewer than three usable fitting or independent verification controls.', report)
    translation = np.median(np.asarray(fits), axis=0)
    if max(float(np.linalg.norm(v-translation)) for v in fits) > residual:
        raise QualityBlocked('Fitting controls disagree; a single translation is not justified.', report)
    report['translationPixels'] = translation.tolist()
    latitude = float(np.mean([c['region']['south']+c['region']['north'] for c, *_ in chips])/2)
    metres = [float(translation[0]*before.transform.e*111195), float(translation[1]*before.transform.a*111195*math.cos(math.radians(latitude)))]
    report['translationNorthEastMetres'] = metres
    errors, variations = [], []
    for control, item, window, _ in chips:
        if control['role'] != 'Verify': continue
        a = before.read(1, window=window, masked=True).astype('float32').filled(np.nan)
        corrected = aligned_power(after, a.shape, before.window_transform(window), translation)
        try:
            offset, ncc = estimate(a, corrected, maximum, correlation)
            error = float(np.linalg.norm(offset)); errors.append(error)
            delta = 10*np.log10(corrected)-10*np.log10(a)
            median = float(np.median(delta)); mad = float(np.median(np.abs(delta-median)))
            item.update(afterResidualPixels=offset.tolist(), residualMagnitudePixels=error, afterCorrelation=ncc,
                        afterResidualNorthEastMetres=[float(offset[0]*before.transform.e*111195),float(offset[1]*before.transform.a*111195*math.cos(math.radians(latitude)))],
                        stableMedianDeltaDb=median, stableMADDb=mad)
            variations.append(dict(medianDeltaDb=median, madDb=mad))
        except ValueError as e:
            item.update(state='failed', reason=str(e))
    if len(errors) < 3 or max(errors) > residual:
        raise QualityBlocked('Independent verification did not meet the configured residual limit.', report)
    report.update(state='passed', maximumVerificationResidualPixels=max(errors), stableRadiometricStatistics=variations)
    return translation, report
