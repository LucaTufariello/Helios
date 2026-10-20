"""Generated RGB fixtures test geocoding/display semantics, not sensor accuracy."""
import copy
import builtins
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

    def test_missing_rasterio_identifies_environment_and_setup_instead_of_network(self):
        request=self.root/'request.json';request.write_text(json.dumps(self.request))
        target=self.root/'missing-rasterio'
        original_import=builtins.__import__
        def missing(name,*args,**kwargs):
            if name=='rasterio':raise ModuleNotFoundError("No module named 'rasterio'",name='rasterio')
            return original_import(name,*args,**kwargs)
        with patch('builtins.__import__',side_effect=missing):
            with self.assertRaises(ModuleNotFoundError):optical.run(request,target)
        result=json.loads((target/'result.json').read_text())
        self.assertEqual(result['state'],'failed')
        self.assertIn('missing Rasterio',result['error'])
        self.assertIn('Engine and jobs',result['error'])
        self.assertNotIn('connection',result['error'])
        self.assertIn('ModuleNotFoundError',(target/'diagnostics.log').read_text())
        self.assertFalse((target/'preview.png').exists())

    def test_remote_read_reopens_after_interruption_without_stale_cache(self):
        from rasterio.errors import RasterioIOError
        events=[]
        actual_open=rasterio.open
        calls=[]
        def open_source(*args, **kwargs):
            if str(args[0]).startswith('https:'):
                calls.append(rasterio.env.getenv())
                return actual_open(self.source)
            return actual_open(*args, **kwargs)
        real_render=optical.render_dataset
        count=0
        def interrupted(*args):
            nonlocal count
            count+=1
            if count==1:raise RasterioIOError('TIFFReadEncodedTile() failed: short transfer')
            return real_render(*args)
        with patch.object(rasterio,'open',side_effect=open_source), patch.object(optical,'render_dataset',side_effect=interrupted), patch.object(optical.time,'sleep'):
            result=optical.render_remote(SCENE,self.bounds,self.destination,'rgb','sentinel2',lambda *e:events.append(e))
        self.assertGreater(result['validFraction'],0)
        self.assertEqual(len(calls),2)
        self.assertIn('/vsicurl/'+SCENE['visualURL'],calls[0]['CPL_VSIL_CURL_NON_CACHED'])
        self.assertEqual(calls[1]['GDAL_HTTP_MULTIRANGE'],'SERIAL')
        self.assertEqual(calls[1]['GDAL_HTTP_VERSION'],'1.1')
        self.assertTrue(any('retrying' in e[1] for e in events))
        self.assertIn('short transfer',(self.destination/'diagnostics.log').read_text())
        with rasterio.open(self.destination/'true-color.tif') as output:
            self.assertEqual(output.crs.to_epsg(),4326)

    def test_remote_retry_exhaustion_is_bounded_and_cleans_partial_files(self):
        from rasterio.errors import RasterioIOError
        request=self.root/'request.json';request.write_text(json.dumps(self.request))
        target=self.root/'retries-failed'
        calls=0
        def fail(*args, **kwargs):
            nonlocal calls
            calls+=1
            (target/'preview.png').write_bytes(b'partial')
            raise RasterioIOError('Read failed: '+('diagnostic detail '*2000))
        with patch.object(rasterio,'open',side_effect=fail), patch.object(optical.time,'sleep'):
            with self.assertRaisesRegex(optical.OpticalError,'after 3 attempts'):optical.run(request,target)
        self.assertEqual(calls,3)
        self.assertFalse((target/'preview.png').exists())
        self.assertLessEqual(len((target/'diagnostics.log').read_text()),16000)
        self.assertEqual(json.loads((target/'result.json').read_text())['state'],'failed')
        self.assertNotIn('diagnostic detail',json.loads((target/'result.json').read_text())['error'])

    def test_unavailable_remote_asset_is_not_retried(self):
        from rasterio.errors import RasterioIOError
        with patch.object(rasterio,'open',side_effect=RasterioIOError('HTTP response code: 404')) as opener, patch.object(optical.time,'sleep') as sleep:
            with self.assertRaisesRegex(optical.OpticalError,'unavailable from the archive'):
                optical.render_remote(SCENE,self.bounds,self.destination,'rgb','sentinel2',lambda *e:None)
        self.assertEqual(opener.call_count,1)
        sleep.assert_not_called()

    def test_export_failure_does_not_restart_remote_reads(self):
        from rasterio.errors import RasterioIOError
        actual_open=rasterio.open
        def fail_export(dataset,bounds,destination,scene,emit):
            emit('exporting','Saving',.9)
            raise RasterioIOError('Disk full')
        with patch.object(rasterio,'open',side_effect=lambda *a, **kw: actual_open(self.source)) as opener, patch.object(optical,'render_dataset',side_effect=fail_export), patch.object(optical.time,'sleep') as sleep:
            with self.assertRaisesRegex(optical.OpticalError,'could not be saved'):
                optical.render_remote(SCENE,self.bounds,self.destination,'rgb','sentinel2',lambda *e:None)
        self.assertEqual(opener.call_count,1)
        sleep.assert_not_called()

    def test_cancel_during_retry_backoff_stops_without_new_attempt(self):
        from rasterio.errors import RasterioIOError
        with patch.object(rasterio,'open',side_effect=RasterioIOError('Read failed')) as opener, patch.object(optical.time,'sleep',side_effect=optical.OpticalError('Optical image request cancelled.')):
            with self.assertRaisesRegex(optical.OpticalError,'cancelled'):
                optical.render_remote(SCENE,self.bounds,self.destination,'rgb','sentinel2',lambda *e:None)
        self.assertEqual(opener.call_count,1)


    def test_truncated_http_response_recovers_with_real_gdal(self):
        from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
        import threading
        payload=self.source.read_bytes()
        requests=[]
        class Handler(BaseHTTPRequestHandler):
            def log_message(self, *args):pass
            def do_HEAD(self):
                self.send_response(200)
                self.send_header('Content-Length',str(len(payload)))
                self.send_header('Accept-Ranges','bytes')
                self.end_headers()
            def do_GET(self):
                value=self.headers.get('Range','bytes=0-'+str(len(payload)-1))
                start,end=map(int,value.removeprefix('bytes=').split('-'))
                end=min(end,len(payload)-1)
                body=payload[start:end+1]
                requests.append((start,end))
                self.send_response(206)
                self.send_header('Content-Length',str(len(body)))
                self.send_header('Content-Range',f'bytes {start}-{end}/{len(payload)}')
                self.end_headers()
                # Correct headers, abruptly truncated body: same failure class as
                # the real archive response, without external network dependence.
                self.wfile.write(body[:512] if len(requests)==1 else body)
                self.close_connection=True
        server=ThreadingHTTPServer(('127.0.0.1',0),Handler)
        thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start()
        try:
            scene=dict(SCENE,visualURL=f'http://127.0.0.1:{server.server_port}/rgb.tif')
            events=[]
            with rasterio.Env(GDAL_DISABLE_READDIR_ON_OPEN='EMPTY_DIR',GDAL_HTTP_MAX_RETRY='0',GDAL_HTTP_TIMEOUT='3'), patch.object(optical.time,'sleep'):
                result=optical.render_remote(scene,self.bounds,self.destination,'rgb','sentinel2',lambda *e:events.append(e))
            self.assertGreater(result['validFraction'],0)
            self.assertTrue(any('retrying' in event[1] for event in events))
            self.assertGreaterEqual(len(requests),2)
            with rasterio.open(self.destination/'true-color.tif') as raster:
                self.assertEqual(tuple(raster.read()[:,30,30]),(120,80,40,255))
        finally:
            server.shutdown();server.server_close();thread.join(timeout=2)


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
