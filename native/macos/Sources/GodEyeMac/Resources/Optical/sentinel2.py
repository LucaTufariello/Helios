"""Dated Sentinel-2 L2A true-colour COG window viewer, not a reflectance processor.
Only the public Collection-1 TCI archive is accessed. No account, shell or SNAP.
Scientific spectral bands are never substituted by this 8-bit display product.
"""
import argparse
import datetime as dt
import json
import math
import os
from pathlib import Path
import re
import signal
import time
import traceback
from urllib.parse import urlsplit

MAX_SIDE = 2048
MAX_JSON = 1024 * 1024


class OpticalError(Exception):
    pass


def read_json(path):
    with Path(path).open('rb') as source:
        data = source.read(MAX_JSON + 1)
    if len(data) > MAX_JSON:
        raise OpticalError('Optical request metadata exceeds the size limit.')
    return json.loads(data)


def save_json(path, value):
    temporary = path.with_suffix('.tmp')
    temporary.write_text(json.dumps(value, allow_nan=False, indent=2), encoding='utf-8')
    temporary.replace(path)


def validate_request(request):
    scene, region = request['scene'], request['region']
    identifier = scene['id']
    if not isinstance(identifier, str) or not re.fullmatch(r'S2[ABC]_T[A-Z0-9]{5}_\d{8}T\d{6}_L2A', identifier):
        raise OpticalError('Only Sentinel-2 Collection-1 L2A scenes are supported.')
    date = dt.datetime.fromisoformat(scene['acquiredAt'].replace('Z', '+00:00'))
    if date.tzinfo is None:
        raise OpticalError('The optical acquisition must have a UTC timestamp.')
    cloud = float(scene['cloudCover'])
    if not math.isfinite(cloud) or not 0 <= cloud <= 100:
        raise OpticalError('Invalid scene cloud metadata.')
    url = urlsplit(scene['visualURL'])
    if url.scheme != 'https' or url.netloc != 'e84-earth-search-sentinel-data.s3.us-west-2.amazonaws.com' or url.query or url.fragment or '%' in url.path or '..' in url.path or not url.path.startswith('/sentinel-2-c1-l2a/') or not url.path.endswith('/'+identifier+'/TCI.tif'):
        raise OpticalError('The optical image URL is outside the trusted public archive.')
    bounds = [float(region[k]) for k in ('west', 'south', 'east', 'north')]
    if not all(math.isfinite(x) for x in bounds) or not (-180 <= bounds[0] < bounds[2] <= 180 and -85 <= bounds[1] < bounds[3] <= 85) or max(bounds[2]-bounds[0], bounds[3]-bounds[1]) > 10:
        raise OpticalError('Select a valid optical region of at most ten degrees across.')
    return scene, bounds


def render_dataset(dataset, bounds, destination, scene, emit):
    import numpy as np
    import rasterio
    from rasterio.enums import Resampling, ColorInterp
    from rasterio.transform import from_bounds
    from rasterio.vrt import WarpedVRT
    from rasterio.warp import transform_bounds
    from PIL import Image
    if dataset.count != 3 or dataset.dtypes != ('uint8',)*3 or not dataset.crs or not dataset.crs.is_projected:
        raise OpticalError('Expected a projected three-band Sentinel-2 true-colour GeoTIFF.')
    if dataset.res[0] <= 0 or dataset.res[1] <= 0:
        raise OpticalError('Invalid source pixel spacing.')
    footprint = transform_bounds(dataset.crs, 'EPSG:4326', *dataset.bounds, densify_pts=21)
    # Only render the intersection; keep the full requested boundary in provenance.
    clipped = [max(bounds[0], footprint[0]), max(bounds[1], footprint[1]),
               min(bounds[2], footprint[2]), min(bounds[3], footprint[3])]
    if clipped[0] >= clipped[2] or clipped[1] >= clipped[3]:
        raise OpticalError('This scene does not intersect the requested region.')
    projected = transform_bounds('EPSG:4326', dataset.crs, *clipped, densify_pts=21)
    if not all(math.isfinite(x) for x in projected):
        raise OpticalError('The requested region cannot be projected safely.')
    columns = max(1, math.ceil((projected[2]-projected[0])/dataset.res[0]))
    rows = max(1, math.ceil((projected[3]-projected[1])/dataset.res[1]))
    scale = max(1, columns/MAX_SIDE, rows/MAX_SIDE)
    width, height = min(MAX_SIDE, max(1, math.ceil(columns/scale))), min(MAX_SIDE, max(1, math.ceil(rows/scale)))
    transform = from_bounds(*clipped, width, height)
    emit('processing', 'Reading optical image tiles; slow archive connections can take several minutes', None)
    # Bounded destination arrays. GDAL reads COG ranges/overviews rather than
    # copying a SAFE scene or downloading/loading a complete source raster.
    with WarpedVRT(dataset, crs='EPSG:4326', transform=transform, width=width, height=height,
                   resampling=Resampling.nearest if scale == 1 else Resampling.average, add_alpha=True, warp_mem_limit=64) as view:
        rgba = view.read(out_dtype='uint8')
    valid = rgba[3] > 0
    coverage = float(valid.mean())
    if not coverage:
        raise OpticalError('No valid optical pixels are available in this scene region.')
    emit('exporting', 'Saving dated optical preview and georeferenced display TIFF', .9)
    Image.fromarray(np.moveaxis(rgba, 0, -1)).save(destination/'preview.png')
    with rasterio.open(destination/'true-color.tif', 'w', driver='GTiff', width=width, height=height,
                       count=4, dtype='uint8', crs='EPSG:4326', transform=transform,
                       tiled=True, blockxsize=256, blockysize=256, compress='deflate') as target:
        target.write(rgba)
        target.colorinterp = (ColorInterp.red, ColorInterp.green, ColorInterp.blue, ColorInterp.alpha)
        target.update_tags(quantity='Sentinel-2 L2A provider-rendered true colour', units='8-bit display RGB, not reflectance',
                           source=json.dumps(scene, allow_nan=False), cloudMask='None; scene cloud percentage is not an AOI cloud mask')
    return dict(bounds=clipped, width=width, height=height, crs='EPSG:4326', validFraction=coverage,
                resampling='nearest' if scale == 1 else 'average',
                sourceResolutionMetres=list(dataset.res), preview='preview.png', geotiff='true-color.tif',
                limitations=['Dated optical image, not live imagery.',
                             'Provider-rendered 8-bit true colour; not a scientific reflectance export.',
                             'Cloud percentage describes the whole source tile; clouds and shadows are not automatically masked.',
                             'One selected tile only; requested regions may have partial coverage.',
                             'Large regions are downsampled to a maximum 2048 pixels per side.'])


def extra_sources():
    # Load our packaged helper explicitly: Python -I intentionally excludes cwd.
    import importlib.util
    spec = importlib.util.spec_from_file_location('godeye_optical_sources',Path(__file__).with_name('optical_sources.py'))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def validate_region(region):
    bounds = [float(region[k]) for k in ('west','south','east','north')]
    if not all(math.isfinite(x) for x in bounds) or not (-180 <= bounds[0] < bounds[2] <= 180 and -85 <= bounds[1] < bounds[3] <= 85) or max(bounds[2]-bounds[0],bounds[3]-bounds[1]) > 10:
        raise OpticalError('Select a valid optical region of at most ten degrees across.')
    return bounds


REMOTE_ATTEMPTS = 3


def record_diagnostic(destination, text):
    """Retain bounded developer detail across retries, including successful recovery."""
    path = destination/'diagnostics.log'
    previous = path.read_text(encoding='utf-8') if path.exists() else ''
    path.write_text((previous + text + '\n')[-16000:], encoding='utf-8')


def render_remote(scene, bounds, destination, mode, sensor, emit):
    import rasterio
    from rasterio.errors import RasterioIOError
    if sensor == 'sentinel2':
        urls = [scene['visualURL']]  # Already admitted by validate_request.
        helper = None
    else:
        helper = extra_sources()
        helper.validate_cbers(scene, mode)  # Admit all requested URLs before opening any.
        keys = ['BAND0'] if mode == 'pan' else ['BAND3', 'BAND2', 'BAND1']
        urls = [scene['bands'][key] for key in keys]
    # GDAL's curl cache otherwise outlives dataset.close(), retaining an incomplete
    # range across retries. Discard these specific remote handles on every close.
    # https://gdal.org/en/stable/user/virtual_file_systems.html#vsicurl-http-https-ftp-files-random-access
    non_cached = ':'.join('/vsicurl/' + url for url in urls)
    for attempt in range(1, REMOTE_ATTEMPTS + 1):
        exporting = False
        def stage(state, message, progress=None):
            nonlocal exporting
            exporting = exporting or state == 'exporting'
            emit(state, message, progress)
        options = dict(CPL_VSIL_CURL_NON_CACHED=non_cached)
        if attempt > 1:
            # A fresh, serialized HTTP/1.1 read also avoids a failed multiplexed
            # range transfer. Keep TLS verification and the original acquisition.
            options.update(GDAL_HTTP_MULTIRANGE='SERIAL', GDAL_HTTP_MULTIPLEX='NO',
                           GDAL_HTTP_VERSION='1.1')
        try:
            with rasterio.Env(**options):
                stage('processing', f'Opening public optical raster (attempt {attempt}/{REMOTE_ATTEMPTS})')
                if sensor == 'sentinel2':
                    with rasterio.open(scene['visualURL'], driver='GTiff') as dataset:
                        return render_dataset(dataset, bounds, destination, scene, stage)
                return helper.render_cbers(scene, bounds, destination, mode, stage)
        except RasterioIOError as error:
            record_diagnostic(destination, f'Remote raster attempt {attempt}/{REMOTE_ATTEMPTS}\n' + traceback.format_exc())
            # A disk/export failure is not a remote-read retry. Cancellation and
            # validation exceptions also propagate immediately, without retrying.
            if exporting:
                raise OpticalError('The optical image was read but could not be saved. Check available disk space and folder permissions.') from error
            detail = str(error).lower()
            if any(code in detail for code in ('response code: 401', 'response code: 403', 'response code: 404', 'response code: 410')):
                raise OpticalError('This optical asset is unavailable from the archive. Search again or choose another acquisition.') from error
            if attempt == REMOTE_ATTEMPTS:
                raise OpticalError(f'The archive image could not be read completely after {REMOTE_ATTEMPTS} attempts. Retry the load or choose another acquisition. The diagnostic log contains the transfer details.') from error
            emit('processing', f'Optical image transfer interrupted; retrying with fresh connections (attempt {attempt+1}/{REMOTE_ATTEMPTS})', None)
            time.sleep(2 * attempt)


def run(request_file, destination):
    destination = Path(destination)
    if destination.exists():
        raise OpticalError('The optical destination already exists.')
    destination.mkdir(parents=True, mode=0o700)
    started = time.monotonic()
    result = dict(version=1, state='queued', workerPID=os.getpid(), createdAt=dt.datetime.now(dt.timezone.utc).isoformat())
    def emit(state, stage, progress=None):
        result.update(state=state)
        event = dict(state=state, stage=stage, progress=progress,
                     timestamp=dt.datetime.now(dt.timezone.utc).isoformat(), elapsedSeconds=time.monotonic()-started)
        print(json.dumps(event), flush=True)
        with (destination/'events.jsonl').open('a') as log:
            log.write(json.dumps(event)+'\n')
        save_json(destination/'result.json', result)
    previous = signal.signal(signal.SIGTERM, lambda *_: (_ for _ in ()).throw(OpticalError('Optical image request cancelled.')))
    try:
        emit('validating', 'Checking satellite product and region', .02)
        request = read_json(request_file)
        sensor = request.get('source',request.get('scene',{}).get('source','sentinel2'))
        if sensor == 'sentinel2':
            scene,bounds = validate_request(request)
            result.update(scene=scene,region=request['region'])
        elif sensor == 'cbers4a':
            scene = request['scene']
            bounds = validate_region(request['region'])
            result.update(scene=scene,region=request['region'])
        elif sensor != 'prisma':
            raise OpticalError('Unsupported optical source.')
        import rasterio
        # Large compressed COG tiles can take minutes on slow archive links.
        # Allow an actively transferring response up to ten minutes, while aborting
        # connections that cannot sustain 1 KiB/s for twenty seconds.
        # Explicit GTiff admission excludes network VRTs and their external paths.
        with rasterio.Env(GDAL_DISABLE_READDIR_ON_OPEN='EMPTY_DIR', GDAL_HTTP_TIMEOUT='600',
                          GDAL_HTTP_MAX_RETRY='1', GDAL_HTTP_RETRY_DELAY='1', GDAL_HTTP_CONNECTTIMEOUT='15',
                          GDAL_HTTP_LOW_SPEED_LIMIT='1024', GDAL_HTTP_LOW_SPEED_TIME='20', GDAL_CACHEMAX=64*1024*1024,
                          CPL_VSIL_CURL_CACHE_SIZE=8*1024*1024, GDAL_HTTP_HEADERS='', GDAL_HTTP_HEADER_FILE=''):
            if sensor in ('sentinel2', 'cbers4a'):
                result.update(render_remote(scene, bounds, destination, request.get('mode','pan'), sensor, emit))
            else:
                if request.get('region'):
                    validate_region(request['region'])
                result.update(extra_sources().render_prisma(request,destination,emit))
        result.update(completedAt=dt.datetime.now(dt.timezone.utc).isoformat(), elapsedSeconds=time.monotonic()-started)
        emit('completed', 'Optical image ready', 1)
    except BaseException as error:
        record_diagnostic(destination, traceback.format_exc())
        for file in destination.iterdir():
            if file.name not in ('result.json', 'events.jsonl', 'diagnostics.log'):
                file.unlink()
        if isinstance(error, ModuleNotFoundError):
            package = {'rasterio': 'Rasterio', 'numpy': 'NumPy', 'PIL': 'Pillow', 'h5py': 'h5py'}.get((error.name or '').split('.')[0], 'a required package')
            result['error'] = f'Raster Python is missing {package}. Open Engine and jobs, select your prepared Python environment, then click Check Python raster imports. The Python used is recorded in the job details.'
        else:
            result['error'] = str(error) if isinstance(error, OpticalError) or type(error).__name__ == 'SourceError' else 'Optical image request failed. Check your connection and configured Python raster environment; a local diagnostic log is available.'
        emit('failed', result['error'])
        raise
    finally:
        signal.signal(signal.SIGTERM, previous)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('operation', choices=['render'])
    parser.add_argument('--input', required=True)
    parser.add_argument('--output', required=True)
    args = parser.parse_args()
    try:
        run(args.input, args.output)
    except BaseException:
        return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
