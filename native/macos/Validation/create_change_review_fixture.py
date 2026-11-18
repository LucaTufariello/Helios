"""Create an explicitly labeled isolated native UI validation profile.

No downloaded/real product is modified. Run with --output <new-directory>, then
launch HeliosMac with HELIOS_VALIDATION_ROOT set to that directory. Never used
automatically by the production app. Does not simulate SNAP scientific validity.
"""
import argparse
import datetime as dt
import importlib.util
import json
from pathlib import Path
import shutil
import tempfile
import uuid

import numpy as np
import rasterio
from rasterio.transform import from_origin


def create(root):
    root.mkdir(parents=True, exist_ok=False)
    worker_path = Path(__file__).parents[1]/'Sources/HeliosMac/Resources/SAR/sentinel1.py'
    spec = importlib.util.spec_from_file_location('sentinel1_fixture', worker_path)
    worker = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(worker)
    jobs = root/'SAR/jobs'
    jobs.mkdir(parents=True)
    transform = from_origin(9, 45.1, .00018, .00018)
    now = dt.datetime.now(dt.timezone.utc)
    site = dict(id=str(uuid.uuid4()), name='GENERATED QA · industrial-site fixture', kind='Infrastructure',
                createdAt=now.timestamp()-978307200,
                region=dict(west=9.0009, south=45.0847, east=9.0153, north=45.0991))
    satellite = root/'Satellite'
    satellite.mkdir()
    worker.save_json(satellite/'sites.json', [site])
    directories = []
    options = dict(polarizations=['VV'], applyOrbit=True, speckle=False, pixelSpacing=20, lowPercentile=2, highPercentile=98)
    for index, date in enumerate(['2026-09-01T05:00:00Z', '2026-09-13T05:00:00Z']):
        directory = jobs/str(uuid.uuid4()).upper()
        directory.mkdir()
        values = np.full((96, 96), .1, dtype='float32')
        if index == 1:
            values[30:60, 30:60] = .4
            values[65:80, 55:75] = .025
        with tempfile.TemporaryDirectory() as temporary:
            calibrated = Path(temporary)
            with rasterio.open(calibrated/'VV-calibrated.tif', 'w', driver='GTiff', width=96, height=96, count=1,
                               dtype='float32', crs='EPSG:4326', transform=transform, nodata=-9999) as dst:
                dst.write(values, 1)
            outputs, raster = worker.render_outputs(calibrated, directory, options)
        metadata = dict(mission='Sentinel-1', satellite='Sentinel-1A', mode='IW', productType='GRD',
                        acquisitionStart=date, acquisitionEnd=date, orbitDirection='DESCENDING', relativeOrbit=66,
                        polarizations=['VV'], bands=[dict(polarization='VV', width=96, height=96, sourcePixelSpacing=20)],
                        inputName='GENERATED-QA-NOT-A-REAL-ACQUISITION.SAFE')
        record = dict(version=1, createdAt=date, completedAt=now.isoformat(), state='completed', metadata=metadata,
                      inputName=metadata['inputName'], options=options, outputs=outputs, raster=raster,
                      processingStages=['Calibration', 'Terrain-Correction'], engine=dict(name='GENERATED QA fixture — no SNAP calibration performed', version='fixture'))
        worker.save_json(directory/'result.json', record)
        directories.append(directory)
    worker.save_json(root/'comparison-request.json', dict(site=site, options=dict(polarization='VV', thresholdDb=3, averagingWindow=5, minimumPixels=25),
                     beforeDirectory=str(directories[0]), afterDirectory=str(directories[1])))
    (root/'README.txt').write_text('GENERATED QA DATA ONLY. Not real Sentinel-1 observations or scientific validation. Delete this entire profile after testing.\n')
    return root


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', required=True)
    args = parser.parse_args()
    print(create(Path(args.output)))
