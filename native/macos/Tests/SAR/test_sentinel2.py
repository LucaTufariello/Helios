"""Generated RGB fixtures test geocoding/display semantics, not sensor accuracy."""
import copy
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import numpy as np
import rasterio
from rasterio.transform import from_origin
from rasterio.warp import transform_bounds
from PIL import Image

WORKER = Path(__file__).parents[2]/'Sources/GodEyeMac/Resources/Optical/sentinel2.py'
spec = importlib.util.spec_from_file_location('sentinel2', WORKER)
optical = importlib.util.module_from_spec(spec)
spec.loader.exec_module(optical)
ID = 'S2C_T32TNR_20260927T102053_L2A'
SCENE = dict(id=ID, acquiredAt='2026-09-27T10:28:15.042Z', platform='sentinel-2c', cloudCover=.01,
             bounds=[8.99,45.05,10.42,46.05],
             visualURL=f'https://e84-earth-search-sentinel-data.s3.us-west-2.amazonaws.com/sentinel-2-c1-l2a/32/T/NR/2026/9/{ID}/TCI.tif')


class SentinelOpticalTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.source = self.root/'rgb.tif'
        self.destination = self.root/'out'
        self.destination.mkdir()
        with rasterio.open(self.source, 'w', driver='GTiff', count=3, width=64, height=64,
                           dtype='uint8', crs='EPSG:32632', transform=from_origin(500000,5050000,10,10), nodata=0) as target:
            rgb = np.stack([np.full((64,64),120), np.full((64,64),80), np.full((64,64),40)]).astype('uint8')
            rgb[:,:8,:8] = 0
            target.write(rgb)
            self.bounds = list(transform_bounds(target.crs,'EPSG:4326',*target.bounds,densify_pts=21))
        self.request = dict(scene=copy.deepcopy(SCENE), region=dict(zip(('west','south','east','north'),self.bounds)))

    def render(self, bounds=None):
        events=[]
        with rasterio.open(self.source) as source:
            result=optical.render_dataset(source,bounds or self.bounds,self.destination,SCENE,lambda *e:events.append(e))
        return result, events

    def test_projected_rgb_geocoded_without_changing_channel_mapping(self):
        result, events=self.render()
        with rasterio.open(self.destination/'true-color.tif') as raster:
            self.assertEqual(raster.crs.to_epsg(),4326)
            self.assertEqual(raster.dtypes,('uint8',)*4)
            self.assertEqual(tuple(raster.read()[:,30,30]),(120,80,40,255))
            self.assertIn('not reflectance',raster.tags()['units'])
            self.assertTrue(np.allclose(raster.bounds,result['bounds']))
        with Image.open(self.destination/'preview.png') as image:
            self.assertEqual(image.mode,'RGBA')
        self.assertEqual(events[-1][0],'exporting')
        self.assertLess(result['validFraction'],1)

    def test_native_spacing_render_preserves_discrete_rgb_values_without_smoothing(self):
        # Sharp source transitions must not become an invented gradient at native spacing.
        with rasterio.open(self.source, 'r+') as target:
            checker=(np.indices((64,64)).sum(axis=0)%2*150+50).astype('uint8')
            target.write(np.stack([checker,checker,checker]))
        self.render()
        with rasterio.open(self.destination/'true-color.tif') as image:
            rgba=image.read()
        self.assertTrue(set(np.unique(rgba[0][rgba[3]>0])).issubset({50,200}))

    def test_invalid_source_pixels_stay_transparent(self):
        result,_=self.render()
        with Image.open(self.destination/'preview.png') as image:
            rgba=np.array(image)
        self.assertTrue(np.any(rgba[:,:,3]==0))
        self.assertGreater(result['validFraction'],.8)
        self.assertLess(result['validFraction'],1)

    def test_oversized_regions_are_clipped_and_destination_is_bounded(self):
        bounds=[self.bounds[0]-.3,self.bounds[1]-.3,self.bounds[2]+.3,self.bounds[3]+.3]
        result,_=self.render(bounds)
        self.assertTrue(np.allclose(result['bounds'],self.bounds))
        self.assertLessEqual(max(result['width'],result['height']),2048)
        self.assertGreater(result['width'],1)

    def test_no_intersection_rejected(self):
        with self.assertRaises(optical.OpticalError):self.render([1,1,1.01,1.01])
        self.assertFalse((self.destination/'preview.png').exists())

    def test_untrusted_urls_and_invalid_region_rejected_before_access(self):
        optical.validate_request(self.request)
        for url in ('file:///etc/passwd','http://localhost/TCI.tif',SCENE['visualURL']+'?key=bad',
                    SCENE['visualURL'].replace('/TCI.tif','/../TCI.tif'),SCENE['visualURL'].replace('https://','https://user:pass@'),
                    SCENE['visualURL'].replace('TCI.tif','other.tif')):
            request=copy.deepcopy(self.request);request['scene']['visualURL']=url
            with self.assertRaises(optical.OpticalError):optical.validate_request(request)
        request=copy.deepcopy(self.request);request['region']['west']=float('nan')
        with self.assertRaises(optical.OpticalError):optical.validate_request(request)

    def test_failed_or_cancelled_stage_removes_partial_outputs_and_retains_bounded_diagnostics(self):
        request=self.root/'request.json';request.write_text(json.dumps(self.request))
        target=self.root/'failed'
        def fail(*args, **kwargs):
            (target/'preview.png').write_bytes(b'partial')
            (target/'true-color.tif').write_bytes(b'partial')
            raise optical.OpticalError('Optical image request cancelled.')
        with patch.object(rasterio,'open',side_effect=fail):
            with self.assertRaises(optical.OpticalError):optical.run(request,target)
        result=json.loads((target/'result.json').read_text())
        self.assertEqual(result['state'],'failed')
        self.assertFalse((target/'preview.png').exists());self.assertFalse((target/'true-color.tif').exists())
        self.assertLessEqual(len((target/'diagnostics.log').read_text()),16000)
        self.assertEqual(json.loads((target/'events.jsonl').read_text().splitlines()[0])['state'],'validating')
        self.assertTrue(self.source.exists())

    def test_request_json_size_limit_and_destination_collision(self):
        file=self.root/'large.json';file.write_bytes(b' '*(optical.MAX_JSON+1))
        with self.assertRaises(optical.OpticalError):optical.read_json(file)
        with self.assertRaises(optical.OpticalError):optical.run(file,self.destination)
        self.assertTrue(self.destination.exists())

    def test_downsampling_retains_bounded_rgba_for_large_source(self):
        source=self.root/'large.tif'
        with rasterio.open(source,'w',driver='GTiff',width=3000,height=200,count=3,dtype='uint8',crs='EPSG:32632',transform=from_origin(500000,5050000,10,10),nodata=0) as target:
            target.write(np.full((3,200,3000),150,dtype='uint8'))
            bounds=list(transform_bounds(target.crs,'EPSG:4326',*target.bounds,densify_pts=21))
        with rasterio.open(source) as dataset:
            result=optical.render_dataset(dataset,bounds,self.destination,SCENE,lambda *e:None)
        self.assertEqual(result['width'],2048)
        self.assertLessEqual(result['height'],2048)
        self.assertEqual(result['sourceResolutionMetres'],[10,10])


if __name__=='__main__':unittest.main()
