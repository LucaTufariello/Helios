"""Small generated structures/raster fixtures test the adapter, never pretend to be real SAR."""
import contextlib
import importlib.util
import json
from pathlib import Path
import tempfile
import sys
import subprocess
import time
import os
import signal
import unittest
from unittest.mock import patch
import zipfile
import numpy as np
import rasterio as rio
from rasterio.transform import from_origin

WORKER = Path(__file__).parents[2] / 'Sources/GodEyeMac/Resources/SAR/sentinel1.py'
sys.dont_write_bytecode = True
spec = importlib.util.spec_from_file_location('sar', WORKER)
sar = importlib.util.module_from_spec(spec)
spec.loader.exec_module(sar)


def fixture(parent, satellite='S1C', pols=('VV', 'VH'), product_type='GRD'):
    root = parent / 'generated.SAFE'
    (root/'measurement').mkdir(parents=True)
    (root/'annotation/calibration').mkdir(parents=True)
    (root/'manifest.safe').write_text(f'<xfdu><productType>{product_type}</productType><pass>ASCENDING</pass><relativeOrbitNumber type="start">42</relativeOrbitNumber></xfdu>')
    for pol in pols:
        stem = f'{satellite.lower()}-iw-grd-{pol.lower()}-test'
        header = f'<adsHeader><missionId>{satellite}</missionId><productType>{product_type}</productType><polarisation>{pol}</polarisation><mode>IW</mode><startTime>2025-01-02T03:04:05</startTime><stopTime>2025-01-02T03:04:30</stopTime></adsHeader>'
        (root/f'annotation/{stem}.xml').write_text(f'<product>{header}<imageInformation><numberOfSamples>12</numberOfSamples><numberOfLines>10</numberOfLines><rangePixelSpacing>10</rangePixelSpacing></imageInformation></product>')
        (root/f'annotation/calibration/calibration-{stem}.xml').write_text(f'<calibration>{header}<calibrationVector><sigmaNought>1 2</sigmaNought></calibrationVector></calibration>')
        (root/f'annotation/calibration/noise-{stem}.xml').write_text(f'<noise>{header}<noiseVector><noiseLut>1 2</noiseLut></noiseVector></noise>')
        with rio.open(root/f'measurement/{stem}.tiff', 'w', driver='GTiff', width=12, height=10, count=1, dtype='uint16', transform=from_origin(0, 10, 1, 1)) as dst:
            dst.write(np.ones((10,12), dtype='uint16'), 1)
    return root


class ProductTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
    def tearDown(self): self.temp.cleanup()
    def inspect(self, path):
        with contextlib.closing(sar.ProductFiles(path)) as files:
            return sar.inspect_product(files)
    def test_grd_polarizations_and_metadata(self):
        product = self.inspect(fixture(self.root))
        self.assertEqual(product['polarizations'], ['VH', 'VV'])
        self.assertEqual(product['satellite'], 'Sentinel-1C')
        self.assertEqual(product['relativeOrbit'], 42)
        self.assertEqual(product['orbitDirection'], 'ASCENDING')
        self.assertEqual(product['bands'][0]['width'], 12)
    def test_vv_only(self):
        self.assertEqual(self.inspect(fixture(self.root, pols=('VV',)))['polarizations'], ['VV'])
    def test_reject_slc(self):
        with self.assertRaises(sar.SarError): self.inspect(fixture(self.root, product_type='SLC'))
    def test_missing_calibration(self):
        product = fixture(self.root)
        next((product/'annotation/calibration').glob('calibration-*.xml')).unlink()
        with self.assertRaises(sar.SarError): self.inspect(product)
    def test_dtd_and_external_entities_rejected(self):
        with self.assertRaises(sar.SarError): sar.xml(b'<!DOCTYPE a [<!ENTITY e SYSTEM "file:///etc/passwd">]><a>&e;</a>')
        with self.assertRaises(sar.SarError): sar.xml(b'<broken>')
    def test_symlink_rejected(self):
        product = fixture(self.root)
        (product/'external').symlink_to('/etc/passwd')
        with self.assertRaises(sar.SarError): self.inspect(product)
    def test_manifest_external_reference(self):
        product = fixture(self.root)
        (product/'manifest.safe').write_text('<xfdu><productType>GRD</productType><fileLocation href="../escape"/></xfdu>')
        with self.assertRaises(sar.SarError): self.inspect(product)
    def test_zip_inspection_extraction(self):
        product = fixture(self.root)
        archive = self.root/'safe.zip'
        with zipfile.ZipFile(archive, 'w') as z:
            for p in product.rglob('*'):
                if p.is_file(): z.write(p, p.relative_to(self.root))
        with contextlib.closing(sar.ProductFiles(archive)) as files:
            self.assertEqual(sar.inspect_product(files)['productType'], 'GRD')
            target = self.root/'extracted'
            target.mkdir()
            self.assertTrue(files.prepare(target).is_file())
    def test_zip_traversal_and_bomb(self):
        for name in ('../escape', '/escape', 'a\\escape', '%2e%2e/escape', 'name?query', 'name#fragment'):
            archive = self.root/'bad.zip'
            with zipfile.ZipFile(archive, 'w') as z: z.writestr(name, 'x')
            with self.assertRaises(sar.SarError): sar.ProductFiles(archive)
        archive = self.root/'bomb.zip'
        with zipfile.ZipFile(archive, 'w', compression=zipfile.ZIP_DEFLATED) as z:
            z.writestr('x.SAFE/manifest.safe', b'0' * 2000000)
        with self.assertRaises(sar.SarError): sar.ProductFiles(archive)
    def test_graph_scientific_stages_and_safe_xml(self):
        options = sar.normalize_options({'polarizations':['VV','VH'], 'speckle':True}, {'polarizations':['VV','VH']})
        graph, stages = sar.build_graph('/tmp/input & name/manifest.safe', '/tmp/out', options)
        self.assertIn('input &amp; name', graph)
        self.assertEqual(stages[:6], ['Read','Apply-Orbit-File','Remove-GRD-Border-Noise','ThermalNoiseRemoval','Calibration','Speckle-Filter'])
        self.assertIn('Terrain-Correction', stages)
        root = sar.xml(graph.encode())
        self.assertEqual(sar.values(root, 'outputImageScaleInDb'), ['false'])
        # Geographic SNAP selector avoids the zero-width terrain tile failure;
        # render_outputs separately verifies the actual exported EPSG:4326 CRS.
        self.assertEqual(sar.values(root, 'mapProjection'), ['WGS84(DD)'])
        self.assertEqual(sar.values(root, 'formatName'), ['GeoTIFF-BigTIFF']*2)
        self.assertNotIn('maskOutAreaWithoutElevation', graph)
    def test_already_corrected_noise_is_not_removed_twice(self):
        metadata = {'polarizations':['VV'], 'bands':[{'polarization':'VV','thermalNoiseCorrectionPerformed':True}]}
        options = sar.normalize_options({'polarizations':['VV']}, metadata)
        _, stages = sar.build_graph('/input', '/output', options)
        self.assertNotIn('ThermalNoiseRemoval', stages)
        self.assertIn('Calibration', stages)
    def test_invalid_options(self):
        for options in ({'polarizations':['HH']}, {'polarizations':['VV'], 'pixelSpacing':float('nan')}, {'polarizations':['VV'], 'lowPercentile':99,'highPercentile':2}):
            with self.assertRaises(sar.SarError): sar.normalize_options(options, {'polarizations':['VV']})
    def test_failure_cleanup(self):
        product = fixture(self.root)
        destination = self.root/'job'
        with patch.object(sar.SnapProcessor, 'run', side_effect=sar.SarError('Test engine failure')):
            with self.assertRaises(sar.SarError):
                sar.process(product, destination, sys.executable, {'polarizations':['VV']})
        result = json.loads((destination/'result.json').read_text())
        self.assertEqual(result['state'], 'failed')
        self.assertEqual(result['outputs'], [])
        self.assertFalse(list(destination.glob('.sar-work-*')))
        self.assertFalse(list(destination.glob('*.tif')))
        self.assertTrue((product/'manifest.safe').exists())

    def test_cancellation_terminates_gpt_and_cleans_workspace(self):
        product = fixture(self.root)
        destination = self.root/'cancel-job'
        ready = self.root/'child.pid'
        executable = self.root/'test-gpt'
        executable.write_text('#!/usr/bin/env python3\nimport os,time\nfrom pathlib import Path\nPath(' + repr(str(ready)) + ').write_text(str(os.getpid()))\nwhile True: time.sleep(.1)\n')
        executable.chmod(0o700)
        worker = subprocess.Popen([sys.executable, str(WORKER), 'process', '--input', str(product), '--output', str(destination), '--gpt', str(executable), '--options', '{"polarizations":["VV"]}'], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        try:
            limit = time.monotonic()+15
            while not ready.exists() and worker.poll() is None and time.monotonic()<limit:
                time.sleep(.05)
            self.assertTrue(ready.exists(), 'Test GPT did not start')
            child = int(ready.read_text())
            worker.terminate()
            worker.wait(timeout=10)
            with self.assertRaises(ProcessLookupError): os.kill(child,0)
            self.assertFalse(list(destination.glob('.sar-work-*')))
            result = json.loads((destination/'result.json').read_text())
            self.assertEqual(result['state'],'failed')
            self.assertIn('cancelled',result['error'])
        finally:
            if worker.poll() is None:
                worker.terminate()
                worker.wait(timeout=10)

    def test_completed_job_and_structured_events(self):
        product = fixture(self.root)
        destination = self.root/'job'
        def prepared_fixture(_self, graph, log, work):
            log.write_text('Mocked test engine; no SAR calibration claim.\n')
            for pol in ('VV','VH'):
                with rio.open(work/'calibrated'/f'{pol}-calibrated.tif', 'w', driver='GTiff', width=12, height=10, count=1, dtype='float32', crs='EPSG:4326', transform=from_origin(8,46,.001,.001), nodata=-9999) as dst:
                    dst.write(np.linspace(.001,2,120,dtype='float32').reshape(10,12),1)
        with patch.object(sar.SnapProcessor, 'run', prepared_fixture):
            sar.process(product, destination, sys.executable, {'polarizations':['VV','VH']})
        result = json.loads((destination/'result.json').read_text())
        self.assertEqual(result['state'], 'completed')
        self.assertEqual(len(result['outputs']),3)
        events = [json.loads(line) for line in (destination/'events.jsonl').read_text().splitlines()]
        self.assertEqual(events[0]['state'], 'validating')
        self.assertEqual(events[-1]['state'], 'completed')
        self.assertTrue(all(e['elapsedSeconds'] >= 0 for e in events))
        self.assertFalse(list(destination.glob('.sar-work-*')))
        with rio.open(destination/'VV-sigma0.tif') as src:
            self.assertIn('Sentinel-1C',src.tags()['processing'])


class RasterTests(unittest.TestCase):
    def test_db_invalid_pixels(self):
        result = sar.linear_to_db(np.array([1, .1, 10, 0, -1, np.nan, np.inf], dtype='float32'))
        np.testing.assert_allclose(result[:3], [0,-10,10], atol=1e-5)
        self.assertTrue(np.isnan(result[3:]).all())
    def test_stretch_percentiles_and_constant(self):
        sample = np.concatenate([np.linspace(-25,0,1000), [10000]])
        limits = sar.display_range(sample, 2, 98)
        self.assertLess(limits[1], 1)
        np.testing.assert_array_equal(sar.stretch(np.array([-100,0,100,np.nan]), [-20,20]), [0,128,255,0])
        self.assertLess(*sar.display_range(np.array([2,2,2]), 2, 98))
    def test_nearly_constant_channel_does_not_amplify_roundoff(self):
        sample = np.array([9.999999,10,10.000001], dtype='float32')
        limits = sar.display_range(sample,2,98)
        self.assertGreaterEqual(limits[1]-limits[0],1)
        self.assertLessEqual(np.ptp(sar.stretch(sample,limits).astype('int16')),1)
    def test_composite_mapping(self):
        result = sar.composite_channels(np.array([-10, np.nan]), np.array([-20,-20]))
        np.testing.assert_allclose(result[:,0], [-10,-20,10])
        self.assertTrue(np.isnan(result[2,1]))
    def test_windowed_outputs_preserve_science_crs_and_alpha(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            calibrated = root/'calibrated'
            calibrated.mkdir()
            output = root/'output'
            output.mkdir()
            for pol, factor in [('VV',1),('VH',.1)]:
                values = np.linspace(.001,2,600*300, dtype='float32').reshape(300,600)*factor
                values[0,:5] = [0,-1,np.nan,np.inf,-9999]
                with rio.open(calibrated/f'{pol}-calibrated.tif', 'w', driver='GTiff', width=600, height=300, count=1, dtype='float32', crs='EPSG:4326', transform=from_origin(8,46,.001,.001), nodata=-9999) as dst: dst.write(values,1)
            options = sar.normalize_options({'polarizations':['VV','VH']}, {'polarizations':['VV','VH']})
            products, metadata = sar.render_outputs(calibrated, output, options)
            self.assertEqual(len(products),3)
            with rio.open(output/'VV-sigma0.tif') as src:
                self.assertEqual(src.crs.to_epsg(),4326)
                self.assertEqual(src.dtypes, ('float32',))
                self.assertEqual(src.read(1)[0,0],0)
                self.assertEqual(src.read(1)[0,1],-1)
                self.assertEqual(src.read(1)[0,2],-9999)
                self.assertAlmostEqual(src.read(1)[200,100], values[200,100]*10, places=5)
            with rio.open(output/'Composite-display.tif') as src:
                self.assertEqual(src.count,4)
                self.assertEqual(src.read(4)[0,0],0)
                self.assertEqual(src.read(4)[50,50],255)
            self.assertTrue((output/'Composite-preview.png').is_file())
    def test_missing_georeferencing_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            with rio.open(root/'VV-calibrated.tif', 'w', driver='GTiff', width=3, height=3, count=1, dtype='float32') as dst:
                dst.write(np.ones((3,3),dtype='float32'),1)
            with self.assertRaises(sar.SarError): sar.render_outputs(root, root, {'polarizations':['VV']})

if __name__ == '__main__': unittest.main()
