"""Local Sentinel-1 GRD worker. SNAP owns all SAR calibration/geocoding.
Protocol: bounded JSON-lines status on stdout; persisted result.json on completion.
No shell, server, optical-image approximation, or synthetic georeferencing.
"""
import argparse
import contextlib
import datetime as dt
import json
import math
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import signal
import stat
import subprocess
import tempfile
import time
import traceback
import unicodedata
import xml.etree.ElementTree as ET
import zipfile

MAX_XML = 16 * 1024 * 1024
MAX_FILES = 20000
MAX_BYTES = 40 * 1024**3
NODATA = -9999.0
MIN_DISPLAY_SPAN_DB = 1.0
JOB_DIRECTORY = None
JOB_START = None

class SarError(Exception):
    pass


def emit(state, stage, progress=None):
    event = dict(state=state, stage=stage, progress=progress)
    print(json.dumps(event), flush=True)
    if JOB_DIRECTORY is not None:
        event.update(timestamp=dt.datetime.now(dt.timezone.utc).isoformat(), elapsedSeconds=time.monotonic()-JOB_START)
        with (JOB_DIRECTORY/'events.jsonl').open('a', encoding='utf-8') as log:
            log.write(json.dumps(event) + '\n')
        record = JOB_DIRECTORY/'result.json'
        if record.exists():
            result = json.loads(record.read_text())
            result.update(state=state, lastStage=stage, updatedAt=event['timestamp'])
            save_json(record, result)


def save_json(path, value):
    temp = path.with_suffix('.tmp')
    temp.write_text(json.dumps(value, indent=2, allow_nan=False), encoding='utf-8')
    temp.replace(path)


def safe_name(name):
    if '\\' in name or any(c in name for c in ('%', '?', '#')) or any(ord(c) < 32 for c in name):
        raise SarError('Unsafe product path.')
    p = PurePosixPath(name)
    if p.is_absolute() or any(x in ('..', '') for x in p.parts) or ':' in name:
        raise SarError('Unsafe product path.')
    return str(p)


def xml(data):
    if len(data) > MAX_XML:
        raise SarError('Product XML exceeds the size limit.')
    try:
        text = data.decode('utf-8-sig')
        if re.search(r'<!\s*(DOCTYPE|ENTITY)', text, re.I):
            raise SarError('DTD and entity declarations are not allowed.')
        root = ET.fromstring(text)
        if sum(1 for _ in root.iter()) > 250000:
            raise SarError('Product XML contains too many elements.')
        return root
    except (UnicodeError, ET.ParseError):
        raise SarError('Malformed product XML.') from None


def tag(node):
    return node.tag.rsplit('}', 1)[-1]


def values(root, name):
    return [(n.text or '').strip() for n in root.iter() if tag(n) == name]


def first(root, name, default=''):
    return next(iter(values(root, name)), default)


class ProductFiles:
    """Inspect ZIP in place; extract once only after admission. SAFE is read in place."""
    def __init__(self, path):
        self.path = Path(path).absolute()
        self.archive = None
        if self.path.suffix.lower() == '.zip':
            try:
                self.archive = zipfile.ZipFile(self.path)
                infos = self.archive.infolist()
                if len(infos) > MAX_FILES:
                    raise SarError('Product has too many files.')
                self.names = {}
                folded_names = set()
                total = 0
                for item in infos:
                    name = safe_name(item.filename)
                    mode = item.external_attr >> 16
                    if stat.S_ISLNK(mode) or (stat.S_IFMT(mode) not in (0, stat.S_IFREG, stat.S_IFDIR)):
                        raise SarError('Links and special files are not allowed in products.')
                    if item.flag_bits & 1:
                        raise SarError('Encrypted ZIP products are not supported.')
                    folded = unicodedata.normalize('NFC', name).casefold()
                    if folded in folded_names:
                        raise SarError('Duplicate product paths are not allowed.')
                    total += item.file_size
                    if total > MAX_BYTES or item.file_size > MAX_BYTES or item.file_size > max(1, item.compress_size) * 1000:
                        raise SarError('ZIP product exceeds extraction limits.')
                    folded_names.add(folded)
                    self.names[name] = item
                manifests = [n for n in self.names if n.endswith('.SAFE/manifest.safe')]
                if len(manifests) != 1:
                    raise SarError('Select a ZIP containing exactly one Sentinel-1 SAFE product.')
                self.prefix = manifests[0][:-len('manifest.safe')]
                if any(not n.startswith(self.prefix) and n != self.prefix.rstrip('/') for n in self.names):
                    raise SarError('ZIP may only contain the selected SAFE product.')
                self.files = {n[len(self.prefix):] for n, i in self.names.items() if not i.is_dir()}
                self.total = total
            except SarError:
                self.close()
                raise
            except (OSError, zipfile.BadZipFile):
                self.close()
                raise SarError('Unable to read ZIP product.') from None
        else:
            self.root = self.path.parent if self.path.name == 'manifest.safe' else self.path
            if self.root.is_symlink() or not self.root.is_dir() or not self.root.name.endswith('.SAFE'):
                raise SarError('Select a Sentinel-1 .SAFE directory, manifest.safe, or SAFE ZIP.')
            self.files = set()
            total = 0
            for parent, dirs, files in os.walk(self.root, followlinks=False):
                for name in dirs + files:
                    p = Path(parent) / name
                    if p.is_symlink():
                        raise SarError('Symbolic links are not allowed inside SAFE products.')
                for name in files:
                    p = Path(parent) / name
                    if not p.is_file():
                        raise SarError('Special files are not allowed inside SAFE products.')
                    self.files.add(safe_name(p.relative_to(self.root).as_posix()))
                    total += p.stat().st_size
                    if len(self.files) > MAX_FILES or total > MAX_BYTES:
                        raise SarError('SAFE product exceeds size/file limits.')
            self.total = total
        if 'manifest.safe' not in self.files:
            raise SarError('SAFE product is missing manifest.safe.')

    def close(self):
        if self.archive:
            self.archive.close()

    def read(self, name):
        if name not in self.files:
            raise SarError('Required product metadata is missing.')
        if self.archive:
            info = self.names[self.prefix + name]
            if info.file_size > MAX_XML:
                raise SarError('Product XML exceeds the size limit.')
            with self.archive.open(info) as f:
                return f.read(MAX_XML + 1)
        p = self.root / name
        if p.stat().st_size > MAX_XML:
            raise SarError('Product XML exceeds the size limit.')
        return p.read_bytes()

    def signature(self, name):
        if self.archive:
            with self.archive.open(self.names[self.prefix + name]) as f:
                return f.read(8)
        with (self.root / name).open('rb') as f:
            return f.read(8)

    def prepare(self, directory):
        if not self.archive:
            return self.root / 'manifest.safe'
        if shutil.disk_usage(directory).free < self.total + 2 * 1024**3:
            raise SarError('Not enough free space to extract this SAFE product.')
        for name, info in self.names.items():
            target = directory / name
            if info.is_dir():
                target.mkdir(parents=True, exist_ok=True)
                continue
            target.parent.mkdir(parents=True, exist_ok=True)
            # Exclusive creation, bounded streaming, CRC validated by zipfile.
            with self.archive.open(info) as src, target.open('xb') as dst:
                written = 0
                while block := src.read(1024 * 1024):
                    written += len(block)
                    if written > info.file_size:
                        raise SarError('Invalid ZIP entry size.')
                    dst.write(block)
        return directory / self.prefix / 'manifest.safe'


def inspect_product(files):
    manifest = xml(files.read('manifest.safe'))
    if tag(manifest).lower() != 'xfdu' or first(manifest, 'productType') != 'GRD':
        raise SarError('Only Sentinel-1 Level-1 GRD SAFE products are supported (not SLC/OCN).')
    for node in manifest.iter():
        if tag(node) == 'fileLocation':
            href = node.attrib.get('href', '')
            name = safe_name(href.removeprefix('./'))
            if name not in files.files:
                raise SarError('A file referenced by manifest.safe is missing.')
    # Reject unsafe XML even in auxiliary documents that SNAP may read.
    for name in files.files:
        if name.lower().endswith(('.xml', '.kml')):
            xml(files.read(name))
    bands = []
    for name in sorted(files.files):
        if not re.fullmatch(r'annotation/[^/]+\.xml', name):
            continue
        root = xml(files.read(name))
        mission = first(root, 'missionId')
        pol = first(root, 'polarisation')
        if mission not in ('S1A', 'S1B', 'S1C', 'S1D') or first(root, 'productType') != 'GRD':
            raise SarError('Annotation is not a supported Sentinel-1 GRD product.')
        if first(root, 'mode') not in ('IW', 'EW', 'SM'):
            raise SarError('This version supports IW, EW and SM GRD modes.')
        if pol not in ('VV', 'VH', 'HH', 'HV'):
            raise SarError('Unknown polarization in product annotation.')
        stem = Path(name).stem
        required = [f'measurement/{stem}.tiff', f'annotation/calibration/calibration-{stem}.xml',
                    f'annotation/calibration/noise-{stem}.xml']
        if any(n not in files.files for n in required):
            raise SarError('Measurement, calibration, or noise metadata is missing.')
        signature = files.signature(required[0])
        if signature[:4] not in (b'II*\x00', b'MM\x00*', b'II+\x00', b'MM\x00+'):
            raise SarError('Measurement is not a TIFF file.')
        calibration = xml(files.read(required[1]))
        noise = xml(files.read(required[2]))
        if tag(calibration) != 'calibration' or not values(calibration, 'sigmaNought'):
            raise SarError('Sigma0 calibration lookup tables are missing.')
        if tag(noise) != 'noise' or not (values(noise, 'noiseLut') or values(noise, 'noiseRangeLut')):
            raise SarError('Thermal noise lookup tables are missing.')
        for metadata in (calibration, noise):
            if first(metadata, 'missionId') != mission or first(metadata, 'polarisation') != pol:
                raise SarError('Calibration/noise metadata does not match the measurement.')
        try:
            width, height = int(first(root, 'numberOfSamples')), int(first(root, 'numberOfLines'))
            spacing = float(first(root, 'rangePixelSpacing'))
            start, end = first(root, 'startTime'), first(root, 'stopTime')
            for date in (start, end):
                dt.datetime.fromisoformat(date.replace('Z', '+00:00'))
            if not 0 < width <= 200000 or not 0 < height <= 200000 or not math.isfinite(spacing) or spacing <= 0:
                raise ValueError()
        except ValueError:
            raise SarError('Invalid acquisition time or raster dimensions.') from None
        bands.append(dict(polarization=pol, satellite=mission, mode=first(root, 'mode'),
                          width=width, height=height, acquisitionStart=start, acquisitionEnd=end,
                          sourcePixelSpacing=spacing, annotation=name,
                          thermalNoiseCorrectionPerformed=first(root, 'thermalNoiseCorrectionPerformed', 'false').lower() in ('true','1')))
    supported = [b for b in bands if b['polarization'] in ('VV', 'VH')]
    if not supported or len({b['polarization'] for b in supported}) != len(supported):
        raise SarError('This version requires one VV and/or VH GRD measurement per polarization.')
    reference = bands[0]
    if any(any(b[k] != reference[k] for k in ('satellite', 'mode', 'acquisitionStart', 'acquisitionEnd')) for b in bands):
        raise SarError('Product annotations have inconsistent acquisition metadata.')
    orbit = next((n.text.strip() for n in manifest.iter() if tag(n) == 'relativeOrbitNumber'
                  and n.attrib.get('type') == 'start' and n.text), None)
    return dict(mission='Sentinel-1', satellite='Sentinel-1' + reference['satellite'][-1],
                productType='GRD', mode=reference['mode'], acquisitionStart=reference['acquisitionStart'],
                acquisitionEnd=reference['acquisitionEnd'], orbitDirection=first(manifest, 'pass', 'Unknown'),
                relativeOrbit=int(orbit) if orbit else None, bands=supported,
                polarizations=[b['polarization'] for b in supported], inputName=files.path.name)


def build_graph(manifest, output, options):
    graph = ET.Element('graph', id='GodEyeSentinel1GRD')
    ET.SubElement(graph, 'version').text = '1.0'
    previous = None
    stages = []
    def node(identifier, operator, parameters):
        nonlocal previous
        n = ET.SubElement(graph, 'node', id=identifier)
        ET.SubElement(n, 'operator').text = operator
        sources = ET.SubElement(n, 'sources')
        if previous:
            ET.SubElement(sources, 'sourceProduct', refid=previous)
        params = ET.SubElement(n, 'parameters')
        for key, value in parameters.items():
            ET.SubElement(params, key).text = str(value).lower() if isinstance(value, bool) else str(value)
        previous = identifier
        stages.append(operator)
    selected = ','.join(options['polarizations'])
    node('read', 'Read', {'file': manifest})
    if options['applyOrbit']:
        node('orbit', 'Apply-Orbit-File', {'orbitType': 'Sentinel Precise (Auto Download)', 'polyDegree': 3, 'continueOnFail': False})
    node('border', 'Remove-GRD-Border-Noise', {'selectedPolarisations': selected})
    if options.get('removeThermalNoise', True):
        node('thermal', 'ThermalNoiseRemoval', {'selectedPolarisations': selected, 'removeThermalNoise': True, 'clipNegativeValues': False})
    node('calibration', 'Calibration', {'selectedPolarisations': selected, 'outputSigmaBand': True,
                                       'outputGammaBand': False, 'outputBetaBand': False,
                                       'outputImageScaleInDb': False})
    if options['speckle']:
        node('speckle', 'Speckle-Filter', {'filter': 'Refined Lee'})
    node('terrain', 'Terrain-Correction', {'sourceBands': ','.join('Sigma0_' + p for p in options['polarizations']),
         'demName': 'Copernicus 30m Global DEM', 'demResamplingMethod': 'BILINEAR_INTERPOLATION',
         'imgResamplingMethod': 'BILINEAR_INTERPOLATION', 'pixelSpacingInMeter': options['pixelSpacing'],
         # SNAP's WGS84(DD) selects longitude/latitude axes and degree spacing.
         # EPSG:4326 triggers a zero tile width in SNAP 14.0.1 terrain correction
         # on a real GRD product. The exported CRS is still checked as EPSG:4326.
         'mapProjection': 'WGS84(DD)', 'nodataValueAtSea': True,
         'saveSelectedSourceBand': True, 'applyRadiometricNormalization': False})
    terrain_node = previous
    for pol in options['polarizations']:
        previous = terrain_node
        node('select-' + pol, 'BandSelect', {'sourceBands': 'Sigma0_' + pol})
        node('write-' + pol, 'Write', {'file': str(Path(output) / (pol + '-calibrated.tif')), 'formatName': 'GeoTIFF-BigTIFF'})
    return ET.tostring(graph, encoding='unicode'), stages


def linear_to_db(array):
    import numpy as np
    result = np.full(array.shape, np.nan, dtype=np.float32)
    valid = np.isfinite(array) & (array > 0)
    result[valid] = 10 * np.log10(array[valid])
    return result


def display_range(sample, low, high):
    import numpy as np
    sample = sample[np.isfinite(sample)]
    if not sample.size:
        raise SarError('No valid backscatter pixels in processed output.')
    a, b = np.percentile(sample, [low, high]).tolist()
    if b - a < MIN_DISPLAY_SPAN_DB:
        center = (a+b)/2
        a, b = center-MIN_DISPLAY_SPAN_DB/2, center+MIN_DISPLAY_SPAN_DB/2
    return [float(a), float(b)]


def stretch(array, limits):
    import numpy as np
    return np.rint(np.clip(np.nan_to_num((array - limits[0]) / (limits[1] - limits[0]), nan=0), 0, 1) * 255).astype('uint8')


def composite_channels(vv_db, vh_db):
    # False color: R=VV dB, G=VH dB, B=VV dB - VH dB (=10 log10(VV/VH)).
    # Subtraction avoids unstable linear division and excludes nonpositive samples.
    import numpy as np
    return np.stack([vv_db, vh_db, vv_db - vh_db])


def normalize_options(options, metadata):
    pols = options.get('polarizations', [])
    if not pols or len(pols) != len(set(pols)) or any(p not in metadata['polarizations'] for p in pols):
        raise SarError('Select available VV and/or VH polarizations.')
    result = dict(polarizations=pols, applyOrbit=bool(options.get('applyOrbit', True)),
                  speckle=bool(options.get('speckle', False)), pixelSpacing=float(options.get('pixelSpacing', 20)),
                  lowPercentile=float(options.get('lowPercentile', 2)), highPercentile=float(options.get('highPercentile', 98)))
    if not 10 <= result['pixelSpacing'] <= 100 or not 0 <= result['lowPercentile'] < result['highPercentile'] <= 100:
        raise SarError('Invalid pixel spacing or percentile limits.')
    statuses = {b.get('thermalNoiseCorrectionPerformed', False) for b in metadata.get('bands', []) if b['polarization'] in pols}
    if len(statuses) > 1:
        raise SarError('Selected bands have different thermal-noise correction states; process them separately.')
    result['removeThermalNoise'] = statuses != {True}
    return result


def render_outputs(calibrated_directory, destination, options, provenance=None):
    import numpy as np
    import rasterio as rio
    from rasterio.enums import Resampling, ColorInterp
    from rasterio.windows import Window
    from PIL import Image
    sources = {}
    outputs = []
    with contextlib.ExitStack() as stack:
        for pol in options['polarizations']:
            image = calibrated_directory / (pol + '-calibrated.tif')
            if not image.is_file():
                raise SarError('SNAP calibrated band file is missing.')
            sources[pol] = stack.enter_context(rio.open(image))
            if sources[pol].count != 1 or sources[pol].dtypes[0] != 'float32':
                raise SarError('SNAP did not produce a single floating-point Sigma0 band.')
        source = next(iter(sources.values()))
        if source.crs is None or source.crs.to_epsg() != 4326 or source.transform.is_identity or source.transform.b != 0 or source.transform.d != 0 or source.transform.a <= 0 or source.transform.e >= 0:
            raise SarError('Terrain-corrected output has no supported geographic grid. Nothing will be placed on the map.')
        bounds = list(source.bounds)
        if not (-180 <= bounds[0] < bounds[2] <= 180 and -85 <= bounds[1] < bounds[3] <= 85) or bounds[2] - bounds[0] > 180:
            raise SarError('This map overlay supports non-antimeridian scenes between 85°S and 85°N.')
        if source.width * source.height > 2_000_000_000:
            raise SarError('Output grid is too large.')
        for s in sources.values():
            if s.crs != source.crs or s.transform != source.transform or (s.width, s.height) != (source.width, source.height):
                raise SarError('Polarization grids are not aligned; composite creation is unsafe.')
        # Bounded deterministic sampling for display percentiles. No full raster in RAM.
        scale = min(1, 512 / max(source.width, source.height))
        sample_shape = (max(1, round(source.height * scale)), max(1, round(source.width * scale)))
        samples = {p: linear_to_db(s.read(1, out_shape=sample_shape, masked=True, resampling=Resampling.nearest).filled(np.nan)) for p, s in sources.items()}
        channels = dict(samples)
        if set(sources) == {'VV', 'VH'}:
            channels['Composite'] = composite_channels(samples['VV'], samples['VH'])
        ranges = {p: [display_range(c, options['lowPercentile'], options['highPercentile']) for c in (a if a.ndim == 3 else [a])] for p, a in channels.items()}
        required = source.width*source.height*(12*len(sources)+(4 if len(sources)==2 else 0))
        if shutil.disk_usage(destination).free < required + 1024**3:
            raise SarError('Not enough free space for the geocoded scientific and display rasters.')
        profile = dict(driver='GTiff', width=source.width, height=source.height, crs=source.crs,
                       transform=source.transform, tiled=True, blockxsize=256, blockysize=256,
                       compress='deflate', BIGTIFF='IF_SAFER')
        writers = {}
        for pol in sources:
            for quantity in ('sigma0', 'db'):
                path = destination / f'{pol}-{quantity}.tif'
                dst = stack.enter_context(rio.open(path, 'w', **profile, count=1, dtype='float32', nodata=NODATA, predictor=3))
                dst.set_band_description(1, f'Sigma0_{pol}' + ('_dB' if quantity == 'db' else '_linear'))
                dst.update_tags(processing=json.dumps(provenance or {}, allow_nan=False), quantity='Sigma0', units='dB' if quantity == 'db' else 'linear power', polarization=pol)
                writers[(pol, quantity)] = dst
        for name in channels:
            dst = stack.enter_context(rio.open(destination / f'{name}-display.tif', 'w', **profile, count=4, dtype='uint8', predictor=2))
            dst.colorinterp = (ColorInterp.red, ColorInterp.green, ColorInterp.blue, ColorInterp.alpha)
            dst.update_tags(processing=json.dumps(provenance or {}, allow_nan=False), visualization='Radar backscatter; false color is not optical', ranges=json.dumps(ranges[name]))
            writers[(name, 'display')] = dst
        total = math.ceil(source.width / 256) * math.ceil(source.height / 256)
        done = 0
        for y in range(0, source.height, 256):
            for x in range(0, source.width, 256):
                window = Window(x, y, min(256, source.width-x), min(256, source.height-y))
                dbs = {}
                for pol, s in sources.items():
                    linear = s.read(1, window=window, masked=True).astype('float32').filled(np.nan)
                    valid = np.isfinite(linear) & (linear > 0)
                    dbs[pol] = linear_to_db(linear)
                    writers[(pol, 'sigma0')].write(np.where(np.isfinite(linear), linear, NODATA), 1, window=window)
                    writers[(pol, 'db')].write(np.where(valid, dbs[pol], NODATA), 1, window=window)
                displays = dict(dbs)
                if 'Composite' in channels:
                    displays['Composite'] = composite_channels(dbs['VV'], dbs['VH'])
                for name, a in displays.items():
                    a = a if a.ndim == 3 else np.stack([a]*3)
                    rgb = np.stack([stretch(c, ranges[name][i if name == 'Composite' else 0]) for i, c in enumerate(a)])
                    alpha = np.all(np.isfinite(a), axis=0).astype('uint8') * 255
                    writers[(name, 'display')].write(np.concatenate([rgb, alpha[None]]), window=window)
                done += 1
                if done == total or done % max(1, total//100) == 0:
                    emit('exporting', 'Writing calibrated and display rasters', 0.7 + 0.2 * done/total)
        # Close writers before reading the bounded native preview.
        for dst in writers.values():
            dst.close()
        scale = min(1, 2048 / max(source.width, source.height))
        h, w = max(1, round(source.height*scale)), max(1, round(source.width*scale))
        for name in channels:
            with rio.open(destination / f'{name}-display.tif') as display:
                rgba = display.read(out_shape=(4,h,w), resampling=Resampling.nearest)
            preview = f'{name}-preview.png'
            Image.fromarray(np.moveaxis(rgba, 0, -1)).save(destination / preview)
            outputs.append(dict(id=name, preview=preview, visualization=f'{name}-display.tif',
                                scientific=f'{name}-sigma0.tif' if name in sources else None,
                                db=f'{name}-db.tif' if name in sources else None, bounds=bounds,
                                displayRanges=ranges[name], width=source.width, height=source.height))
        return outputs, dict(crs=source.crs.to_string(), dimensions=[source.width, source.height],
                             pixelSpacing=list(source.res), nodata=NODATA,
                             percentileMethod='Nearest-neighbor sample of at most 512×512 valid dB pixels')


class SnapProcessor:
    def __init__(self, executable):
        self.executable = Path(executable)
        self.child = None
        # The portable desktop owns a new worker session and can stop the whole
        # job even if Python is stuck. Preserve the existing standalone/Swift
        # behavior when that explicit process-boundary contract is absent.
        self.shared_session = os.environ.get('GODEYE_WORKER_OWN_SESSION') == '1'

    def stop_child(self):
        if self.child and self.child.poll() is None:
            if self.shared_session:
                self.child.terminate()
            else:
                os.killpg(self.child.pid, signal.SIGTERM)
            try:
                self.child.wait(timeout=5)
            except subprocess.TimeoutExpired:
                if self.shared_session:
                    self.child.kill()
                else:
                    os.killpg(self.child.pid, signal.SIGKILL)
                self.child.wait(timeout=5)

    def cancel(self, *_):
        self.stop_child()
        raise SarError('Processing cancelled.')

    def identify(self):
        # Install layouts vary. Search only conventional SNAP module directories near GPT.
        roots = [self.executable.parent.parent/'snap/modules', self.executable.parent.parent/'modules']
        for root in roots:
            core = root/'org-esa-snap-snap-core.jar'
            if core.is_file():
                with zipfile.ZipFile(core) as jar:
                    info = jar.getinfo('META-INF/MANIFEST.MF')
                    if info.file_size > 65536:
                        continue
                    manifest = jar.read(info).decode('utf-8', errors='replace')
                    version = re.search(r'OpenIDE-Module-Specification-Version:\s*([0-9.]+)', manifest)
                    if version:
                        if int(version.group(1).split('.')[0]) < 14:
                            raise SarError('Use SNAP 14 or later with the Microwave Toolbox; older thermal-noise behavior is not accepted.')
                        return version.group(1)
        return 'Unknown (installation must be SNAP 14 or later)'

    def run(self, graph, log, work):
        if not self.executable.is_file() or not os.access(self.executable, os.X_OK):
            raise SarError('Select an executable SNAP GPT with the Microwave Toolbox installed.')
        # JVM tile cache and parallelism bounded; SNAP VM heap is configured by its installation.
        self.version = self.identify()
        start = time.monotonic()
        with log.open('w', encoding='utf-8') as diagnostic:
            diagnostic.write('GodEye SNAP engine version: ' + self.version + '\n')
            diagnostic.flush()
            self.child = subprocess.Popen([str(self.executable), str(graph), '-c', '512M', '-q', '4', '-e'],
                stdout=diagnostic, stderr=subprocess.STDOUT, cwd=work, start_new_session=not self.shared_session)
            while self.child.poll() is None:
                if time.monotonic()-start > 6*3600:
                    self.stop_child()
                    raise SarError('SNAP exceeded the six-hour processing time limit.')
                time.sleep(0.2)
                if log.stat().st_size > 16*1024*1024:
                    self.stop_child()
                    raise SarError('SNAP exceeded the diagnostic log size limit.')
            if self.child.returncode != 0:
                raise SarError('SNAP processing failed. Check orbit/DEM availability, product integrity and engine compatibility. A bounded diagnostic log is retained locally.')


def process(input_path, destination, executable, raw_options):
    global JOB_DIRECTORY, JOB_START
    destination = Path(destination)
    if destination.exists():
        raise SarError('Processing destination already exists.')
    destination.mkdir(parents=True, mode=0o700)
    result = dict(version=1, inputName=Path(input_path).name, originalInput=str(Path(input_path).absolute()),
                  workerPID=os.getpid(), createdAt=dt.datetime.now(dt.timezone.utc).isoformat(), state='queued', outputs=[])
    processor = SnapProcessor(executable)
    previous_handler = signal.signal(signal.SIGTERM, processor.cancel)
    started = time.monotonic()
    JOB_DIRECTORY, JOB_START = destination, started
    save_json(destination/'result.json', result)
    try:
        emit('validating', 'Reading SAFE metadata', 0.02)
        with contextlib.closing(ProductFiles(input_path)) as files:
            metadata = inspect_product(files)
            options = normalize_options(raw_options, metadata)
            result.update(metadata=metadata, options=options, state='processing', timings={'validationSeconds':time.monotonic()-started})
            save_json(destination/'result.json', result)
            # Validate Python/GDAL dependencies before large extraction or SNAP work.
            import rasterio
            import numpy
            import PIL
            result['rasterEngine'] = dict(rasterio=rasterio.__version__, gdal=rasterio.__gdal_version__, numpy=numpy.__version__)
            if not processor.executable.is_file():
                raise SarError('SNAP GPT is not configured. Install SNAP with the Microwave Toolbox and select its GPT executable.')
            with tempfile.TemporaryDirectory(prefix='.sar-work-', dir=destination) as temporary:
                work = Path(temporary)
                emit('processing', 'Preparing admitted SAFE input', 0.05)
                estimate = sum(b['width']*b['height'] for b in metadata['bands'] if b['polarization'] in options['polarizations']) * 24
                if shutil.disk_usage(destination).free < estimate + (files.total if files.archive else 0) + 2*1024**3:
                    raise SarError('Not enough free disk space for the estimated processing workspace and outputs.')
                manifest = files.prepare(work)
                (work/'calibrated').mkdir()
                graph, stages = build_graph(manifest, work/'calibrated', options)
                (work/'graph.xml').write_text(graph, encoding='utf-8')
                # Store a reproducible graph with private paths replaced by symbolic placeholders.
                template, _ = build_graph('${INPUT_MANIFEST}', '${OUTPUT_DIRECTORY}', options)
                (destination/'processing-graph.xml').write_text(template, encoding='utf-8')
                result['processingStages'] = stages
                emit('processing', 'SNAP: orbit, noise, Sigma0 and terrain correction (progress is indeterminate)', None)
                engine_start = time.monotonic()
                processor.run(work/'graph.xml', destination/'diagnostics.log', work)
                result['timings']['snapSeconds'] = time.monotonic()-engine_start
                result['engine'] = dict(name='ESA SNAP GPT / Microwave Toolbox', executableName=processor.executable.name, version=getattr(processor, 'version', 'Unknown'))
                emit('exporting', 'Converting floating-point backscatter to dB and previews', 0.7)
                result['outputs'], result['raster'] = render_outputs(work/'calibrated', destination, options, dict(metadata=metadata, options=options, engine=result['engine'], stages=stages, dem='Copernicus 30m Global DEM', quantity='Sigma0 linear power (not terrain-flattened Gamma0)'))
        result.update(state='completed', completedAt=dt.datetime.now(dt.timezone.utc).isoformat(), elapsedSeconds=time.monotonic()-started)
        save_json(destination/'result.json', result)
        emit('completed', 'Radar backscatter ready', 1)
    except BaseException as error:
        # Remove partial scientific rasters and all extracted/intermediate data. Keep small diagnostics.
        if not isinstance(error, SarError):
            with (destination/'diagnostics.log').open('a', encoding='utf-8') as log:
                log.write(traceback.format_exc()[-16000:])
        for p in destination.iterdir():
            if p.name not in ('diagnostics.log', 'processing-graph.xml', 'result.json', 'events.jsonl'):
                if p.is_dir(): shutil.rmtree(p)
                else: p.unlink()
        result.update(state='failed', error=str(error) if isinstance(error, SarError) else 'SAR worker failed. Check configured dependencies and the local diagnostic log.', elapsedSeconds=time.monotonic()-started)
        save_json(destination/'result.json', result)
        emit('failed', result['error'])
        raise
    finally:
        processor.stop_child()
        diagnostic = destination/'diagnostics.log'
        if diagnostic.exists() and diagnostic.stat().st_size > 16*1024*1024:
            with diagnostic.open('r+b') as log: log.truncate(16*1024*1024)
        signal.signal(signal.SIGTERM, previous_handler)
        JOB_DIRECTORY, JOB_START = None, None


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('operation', choices=['inspect', 'process', 'compare'])
    parser.add_argument('--input', required=True)
    parser.add_argument('--output', required=True)
    parser.add_argument('--gpt', default='')
    parser.add_argument('--options', default='{}')
    args = parser.parse_args()
    try:
        if args.operation == 'inspect':
            emit('validating', 'Reading SAFE metadata')
            with contextlib.closing(ProductFiles(args.input)) as files:
                metadata = inspect_product(files)
            save_json(Path(args.output), metadata)
            emit('completed', 'GRD product validated', 1)
        elif args.operation == 'process':
            process(args.input, args.output, args.gpt, json.loads(args.options))
        else:
            # Isolated Python mode deliberately omits the script directory
            # from sys.path. Load only our bundled stage at this fixed path.
            import importlib.util
            spec = importlib.util.spec_from_file_location('godeye_change_review', Path(__file__).with_name('change_review.py'))
            stage = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(stage)
            global JOB_DIRECTORY, JOB_START
            JOB_DIRECTORY, JOB_START = Path(args.output), time.monotonic()
            try:
                stage.run(args.input, args.output, emit, save_json)
            finally:
                JOB_DIRECTORY, JOB_START = None, None
    except Exception as error:
        if args.operation == 'inspect':
            emit('failed', str(error) if isinstance(error, SarError) else 'Unable to inspect product.')
        elif args.operation == 'compare' and not (Path(args.output)/'result.json').exists():
            emit('failed', 'Unable to start change review. Check the configured Python raster dependencies.')
        return 1
    return 0

if __name__ == '__main__':
    raise SystemExit(main())
