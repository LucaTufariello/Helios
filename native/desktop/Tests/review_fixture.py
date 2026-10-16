"""Generated QA inputs, never represented as calibrated satellite acquisitions."""
import json
from pathlib import Path
import sys
import uuid
import numpy as np
import rasterio
from rasterio.transform import from_origin
from PIL import Image

root = Path(sys.argv[1])
root.mkdir(parents=True, exist_ok=True)
transform = from_origin(9, 45.1, .00018, .00018)
bounds = [9, 45.08272, 9.01728, 45.1]
directories = []
for index, date in enumerate(('2026-09-01T05:00:00Z', '2026-09-13T05:00:00Z')):
    identity = str(uuid.uuid4())
    owned = root/identity
    directory = owned/'product' if index == 0 else owned
    directory.mkdir(parents=True)
    if index == 0:
        (owned/'run.json').write_text(json.dumps(dict(version=1, id=identity, operation='process', state='completed')))
    power = np.full((96, 96), .1, dtype='float32')
    if index:
        power[30:60, 30:60] = .4
    with rasterio.open(directory/'VV-sigma0.tif', 'w', driver='GTiff', width=96, height=96,
                       count=1, dtype='float32', crs='EPSG:4326', transform=transform, nodata=-9999) as dst:
        dst.write(power, 1)
        dst.update_tags(quantity='Sigma0', units='linear power')
    Image.fromarray(np.full((96, 96, 4), (120, 120, 120, 255), dtype='uint8')).save(directory/'VV-preview.png')
    name = f'GENERATED-QA-{index}-NOT-A-REAL-ACQUISITION.SAFE'
    record = dict(version=1, state='completed', inputName=name,
                  metadata=dict(mission='Sentinel-1', satellite='Sentinel-1A', productType='GRD', mode='IW',
                                acquisitionStart=date, acquisitionEnd=date, orbitDirection='DESCENDING',
                                relativeOrbit=66, polarizations=['VV'], bands=[], inputName=name),
                  options=dict(applyOrbit=True, speckle=False, pixelSpacing=20, polarizations=['VV'], lowPercentile=2, highPercentile=98),
                  processingStages=['Calibration', 'Terrain-Correction'],
                  engine=dict(name='GENERATED QA fixture — no SNAP calibration performed', version='fixture'),
                  outputs=[dict(id='VV', scientific='VV-sigma0.tif', preview='VV-preview.png', width=96, height=96, bounds=bounds)])
    (directory/'result.json').write_text(json.dumps(record, allow_nan=False))
    directories.append(str(directory))
request = dict(site=dict(id=str(uuid.uuid4()), name='GENERATED QA infrastructure site', kind='Infrastructure', createdAt=0,
                         region=dict(west=9.0009, south=45.0847, east=9.0153, north=45.0991)),
               beforeDirectory=directories[0], afterDirectory=directories[1],
               options=dict(polarization='VV', thresholdDb=3, averagingWindow=5, minimumPixels=25))
(root/'request.json').write_text(json.dumps(request, allow_nan=False))
