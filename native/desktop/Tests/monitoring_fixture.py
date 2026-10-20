"""Generated textured controls and a known change. No real satellite measurement."""
import json
from pathlib import Path
import sys
import uuid
import numpy as np
from scipy.ndimage import gaussian_filter, shift
from PIL import Image
import rasterio
from rasterio.transform import from_origin


def create(root):
    root=Path(root); root.mkdir(parents=True, exist_ok=True)
    transform=from_origin(9,45.1,.00018,.00018)
    rng=np.random.default_rng(7301)
    power=np.exp(gaussian_filter(rng.normal(size=(512,512)),.65)*1.2-2).astype('float32')
    changed=power.copy(); changed[220:290,220:290]*=4
    shifted=shift(changed,(.65,-.35),order=1,mode='constant',cval=np.nan,prefilter=False)
    directories=[]
    for i,array in enumerate((power,shifted)):
        d=root/str(uuid.uuid4()); d.mkdir(); directories.append(str(d))
        with rasterio.open(d/'VV-sigma0.tif','w',driver='GTiff',width=512,height=512,count=1,dtype='float32',crs='EPSG:4326',transform=transform,nodata=-9999) as f:
            f.write(np.where(np.isfinite(array),array,-9999).astype('float32'),1); f.update_tags(quantity='Sigma0',units='linear power')
        pixels=np.clip(array/.4*255,0,255); pixels=np.nan_to_num(pixels).astype('uint8'); Image.fromarray(pixels).save(d/'VV-preview.png')
        date=('2016-07-01T05:00:00Z','2020-07-01T05:00:00Z')[i]
        r=dict(version=1,state='completed',inputName='GENERATED-MONITORING.SAFE',metadata=dict(mission='Sentinel-1',satellite='Sentinel-1A',productType='GRD',mode='IW',acquisitionStart=date,acquisitionEnd=date,orbitDirection='DESCENDING',relativeOrbit=66,polarizations=['VV'],bands=[],inputName='GENERATED-MONITORING.SAFE'),options=dict(applyOrbit=True,speckle=False,pixelSpacing=20,polarizations=['VV'],lowPercentile=2,highPercentile=98),processingStages=['Calibration','Terrain-Correction'],engine=dict(name='GENERATED TEST INPUT; no SNAP calibration',version='fixture'),outputs=[dict(id='VV',scientific='VV-sigma0.tif',preview='VV-preview.png',width=512,height=512,bounds=[9,45.00784,9.09216,45.1])])
        (d/'result.json').write_text(json.dumps(r,allow_nan=False))
    controls=[]
    for role,y in (('Fit',30),('Verify',350)):
        for x in (30,190,350):
            controls.append(dict(id=str(uuid.uuid4()),name=f'{role} {x}',role=role,region=dict(west=9+x*.00018,east=9+(x+96)*.00018,north=45.1-y*.00018,south=45.1-(y+96)*.00018)))
    request=dict(site=dict(id=str(uuid.uuid4()),name='GENERATED QA monitoring area',kind='Infrastructure',createdAt=0,region=dict(west=9.03,south=45.04,east=9.06,north=45.07)),beforeDirectory=directories[0],afterDirectory=directories[1],options=dict(polarization='VV',thresholdDb=3,averagingWindow=5,minimumPixels=25),registration=dict(version=1,maximumShiftPixels=2,maximumResidualPixels=.5,minimumCorrelation=.6,controls=controls))
    (root/'request.json').write_text(json.dumps(request,allow_nan=False)); return request

if __name__=='__main__': create(sys.argv[1])
