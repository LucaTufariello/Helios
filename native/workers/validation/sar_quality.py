"""Developer SAR checks, separate from production processing and visualization.

No absolute-accuracy or ground-control validation is implied by these checks.
The optional SNAP probe checks calibration alone against an independent LUT
calculation; noise removal, orbit handling and geocoding require other references.
"""
import argparse
import contextlib
import datetime as dt
import importlib.util
import json
from pathlib import Path
import tempfile
import xml.etree.ElementTree as ET

import numpy as np
import rasterio as rio


def db_comparison(linear, db):
    linear = np.ma.asarray(linear)
    db = np.ma.asarray(db)
    values = np.asarray(linear.data, dtype=np.float64)
    expected_valid = (~np.ma.getmaskarray(linear)) & np.isfinite(values) & (values > 0)
    actual_valid = (~np.ma.getmaskarray(db)) & np.isfinite(db.data)
    both = expected_valid & actual_valid
    residual = np.asarray(db.data, dtype=np.float64)[both] - 10.0 * np.log10(values[both])
    return dict(validPixels=int(both.sum()), invalidMaskMismatches=int((expected_valid != actual_valid).sum()),
                maxDbError=float(np.max(np.abs(residual))) if residual.size else 0.0,
                squaredDbError=float(np.sum(residual**2)),
                nonpositiveLinearPixels=int(((~np.ma.getmaskarray(linear)) & np.isfinite(values) & (values <= 0)).sum()))


def interpolate_sigma(vectors, line, pixel, azimuth_time=None):
    """Independent two-dimensional LUT interpolation, in source raster indices."""
    lines = np.asarray([float(v.findtext('line')) for v in vectors])
    if len(lines) < 2 or np.any(np.diff(lines) <= 0) or not lines[0] <= line <= lines[-1]:
        raise ValueError('Invalid or out-of-range calibration vector lines')
    upper = min(max(int(np.searchsorted(lines, line, side='right')), 1), len(lines)-1)
    lower = upper-1
    values = []
    for index in (lower, upper):
        positions = np.fromstring(vectors[index].findtext('pixel'), sep=' ')
        sigma = np.fromstring(vectors[index].findtext('sigmaNought'), sep=' ')
        if len(positions) != len(sigma) or len(sigma) < 2 or np.any(np.diff(positions) <= 0):
            raise ValueError('Invalid calibration range vectors')
        if not positions[0] <= pixel <= positions[-1]:
            raise ValueError('Calibration pixel outside LUT coverage')
        values.append(float(np.interp(pixel, positions, sigma)))
    if azimuth_time is None:
        weight = (line-lines[lower])/(lines[upper]-lines[lower])
    else:
        times = [dt.datetime.fromisoformat(vectors[index].findtext('azimuthTime')) for index in (lower, upper)]
        weight = (azimuth_time-times[0]).total_seconds()/(times[1]-times[0]).total_seconds()
    result = values[0]*(1-weight)+values[1]*weight
    if not np.isfinite(result) or result <= 0:
        raise ValueError('Invalid calibration factor')
    return result


def audit_outputs(job, record):
    report = {}
    common_grid = None
    for output in record['outputs']:
        if output['id'] not in ('VV', 'VH'):
            continue
        for name in (output['scientific'], output['db']):
            if Path(name).name != name:
                raise ValueError('Output must be a filename in the job directory')
        with rio.open(job/output['scientific']) as linear, rio.open(job/output['db']) as db:
            grid = (linear.crs, linear.transform, linear.width, linear.height)
            if linear.count != 1 or linear.dtypes != ('float32',) or not linear.crs or linear.crs.to_epsg() != 4326:
                raise ValueError('Unsupported scientific raster type/CRS')
            if db.count != 1 or db.dtypes != ('float32',):
                raise ValueError('Unsupported dB raster type')
            if (db.crs, db.transform, db.width, db.height) != grid:
                raise ValueError('Linear/dB raster grids differ')
            if common_grid is not None and grid != common_grid:
                raise ValueError('Polarization grids differ')
            common_grid = grid
            if not np.allclose(list(linear.bounds), output['bounds'], rtol=0, atol=1e-9):
                raise ValueError('Recorded display bounds differ from GeoTIFF bounds')
            total = dict(validPixels=0, invalidMaskMismatches=0, maxDbError=0.0,
                         squaredDbError=0.0, nonpositiveLinearPixels=0)
            for _, window in linear.block_windows(1):
                block = db_comparison(linear.read(1, window=window, masked=True), db.read(1, window=window, masked=True))
                for key, value in block.items():
                    total[key] = max(total[key], value) if key == 'maxDbError' else total[key]+value
            total['rmsDbError'] = float(np.sqrt(total.pop('squaredDbError')/max(1, total['validPixels'])))
            total['totalPixels'] = linear.width*linear.height
            total['validFraction'] = total['validPixels']/total['totalPixels']
            # Numerical tolerance for Float32 conversion, not sensor accuracy.
            total['passed'] = total['validPixels'] > 0 and total['invalidMaskMismatches'] == 0 and total['maxDbError'] < 1e-4
            total['grid'] = dict(crs=str(linear.crs), shape=[linear.height, linear.width],
                                 bounds=list(linear.bounds), transform=list(linear.transform)[:6])
            report[output['id']] = total
    if not report:
        raise ValueError('No scientific polarization outputs')
    return report


def product_checks(files, record, work, gpt):
    manifest_xml = worker.xml(files.read('manifest.safe'))
    coordinates = next((n.text for n in manifest_xml.iter() if worker.tag(n) == 'coordinates'), None)
    source_bounds = None
    if coordinates:
        coordinates = np.fromstring(coordinates.replace(',', ' '), sep=' ').reshape(-1, 2)
        source_bounds = [float(coordinates[:, 1].min()), float(coordinates[:, 0].min()),
                         float(coordinates[:, 1].max()), float(coordinates[:, 0].max())]
    result = dict(sourceFootprintEnvelope=source_bounds,
                  geographicAlignment='Not independently measured against ground control',
                  note='Footprint/grid checks detect gross errors; they do not establish positional accuracy.')
    result['sourceSafeName'] = files.prefix.rstrip('/') if files.archive else files.root.name
    if source_bounds:
        output_bounds = record['outputs'][0]['bounds']
        result['outputEnvelopeDifferenceDegrees'] = [b-a for a, b in zip(source_bounds, output_bounds)]
    if not gpt:
        return result
    manifest = files.prepare(work)
    graph = ET.Element('graph', id='HeliosCalibrationCheck')
    ET.SubElement(graph, 'version').text = '1.0'
    def node(identifier, operator, parent, parameters):
        n = ET.SubElement(graph, 'node', id=identifier)
        ET.SubElement(n, 'operator').text = operator
        sources = ET.SubElement(n, 'sources')
        if parent:
            ET.SubElement(sources, 'sourceProduct', refid=parent)
        params = ET.SubElement(n, 'parameters')
        for key, value in parameters.items():
            ET.SubElement(params, key).text = str(value)
    node('read', 'Read', None, {'file': manifest})
    node('calibration', 'Calibration', 'read', {'selectedPolarisations': ','.join(record['options']['polarizations']),
         'outputSigmaBand': 'true', 'outputImageScaleInDb': 'false'})
    samples = []
    for band in record['metadata']['bands']:
        pol = band['polarization']
        if pol not in record['options']['polarizations']:
            continue
        annotation = Path(band['annotation'])
        annotation_xml = worker.xml(files.read(str(annotation)))
        first_time = dt.datetime.fromisoformat(worker.first(annotation_xml, 'startTime'))
        interval = float(worker.first(annotation_xml, 'azimuthTimeInterval'))
        calibration = worker.xml(files.read('annotation/calibration/calibration-'+annotation.name))
        vectors = [n for n in calibration.iter() if worker.tag(n) == 'calibrationVector']
        measurement = manifest.parent/'measurement'/annotation.with_suffix('.tiff').name
        with rio.open(measurement) as src:
            for i, (xf, yf) in enumerate(((.15, .2), (.5, .5), (.8, .75))):
                x, y = int((src.width-1)*xf), int((src.height-1)*yf)
                dn = float(src.read(1, window=rio.windows.Window(x, y, 1, 1))[0, 0])
                factor = interpolate_sigma(vectors, y, x, first_time+dt.timedelta(seconds=y*interval))
                identifier = pol+'-'+str(i)
                node('subset-'+identifier, 'Subset', 'calibration', {'sourceBands': 'Sigma0_'+pol, 'region': f'{x},{y},1,1'})
                node('write-'+identifier, 'Write', 'subset-'+identifier,
                     {'file': work/(identifier+'.dim'), 'formatName': 'BEAM-DIMAP'})
                samples.append(dict(polarization=pol, line=y, pixel=x, dn=dn, sigmaLut=factor,
                                    independentSigma0=dn**2/factor**2, identifier=identifier))
    path = work/'calibration-check.xml'
    ET.ElementTree(graph).write(path)
    processor = worker.SnapProcessor(gpt)
    processor.run(path, work/'probe.log', work)
    for sample in samples:
        file = work/(sample['identifier']+'.data')/('Sigma0_'+sample['polarization']+'.img')
        with rio.open(file) as src:
            actual = float(src.read(1)[0, 0])
        sample['snapSigma0'] = actual
        sample['differenceDb'] = float(10*np.log10(actual/sample['independentSigma0'])) if actual > 0 and sample['independentSigma0'] > 0 else None
        sample['passed'] = sample['differenceDb'] is not None and abs(sample['differenceDb']) < 1e-4
    result['calibrationOnlySamples'] = samples
    result['calibrationScope'] = 'Independent DN² / interpolated Sigma0 LUT² calculation, before noise removal and geocoding. Not absolute radiometric validation.'
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--job', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    parser.add_argument('--gpt', help='Optional real SNAP calibration-only probe; extracts the admitted input temporarily')
    args = parser.parse_args()
    record = json.loads((args.job/'result.json').read_text())
    if record['state'] != 'completed':
        raise ValueError('Select a completed SAR job')
    spec = importlib.util.spec_from_file_location('sar_worker', Path(__file__).parents[1]/'SAR/sentinel1.py')
    worker = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(worker)
    report = dict(version=1, checkedAt=dt.datetime.now(dt.timezone.utc).isoformat(),
                  jobID=args.job.name, acquisition=record['metadata'],
                  processingParameters=record['options'], engine=record.get('engine'),
                  scope='Numerical integrity and an optional independent calibration calculation. No absolute accuracy certification.',
                  outputChecks=audit_outputs(args.job, record))
    with contextlib.closing(worker.ProductFiles(record['originalInput'])) as files:
        with tempfile.TemporaryDirectory(prefix='helios-sar-quality-') as temporary:
            report['sourceChecks'] = product_checks(files, record, Path(temporary), args.gpt)
    report['remainingChecks'] = ['Independent, dated ground-control measurements with residual errors in metres',
         'Same-acquisition external processor reference with matched radiometry, masks, DEM and sampling',
         'Noise removal, resampling and terrain-stage accuracy', 'Validation across additional products and terrain types']
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2, allow_nan=False)+'\n')
    print(args.report)
    passed = all(check['passed'] for check in report['outputChecks'].values())
    passed = passed and all(sample['passed'] for sample in report['sourceChecks'].get('calibrationOnlySamples', []))
    raise SystemExit(0 if passed else 1)
