"""Small generated scientific rasters, never represented as real acquisitions."""
import copy
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
import uuid
from unittest.mock import patch

import numpy as np
from PIL import Image
import rasterio
from rasterio.transform import from_origin

WORKER = Path(__file__).parents[2]/'Sources/GodEyeMac/Resources/SAR/sentinel1.py'
spec = importlib.util.spec_from_file_location('change_review', WORKER.with_name('change_review.py'))
review = importlib.util.module_from_spec(spec)
spec.loader.exec_module(review)


def save_json(path, value):
    path.write_text(json.dumps(value, allow_nan=False))


class ChangeReviewTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.addCleanup(self.temp.cleanup)
        self.transform = from_origin(9, 45.1, .00018, .00018)
        self.before = self.make_job('2026-09-01T05:00:00', np.full((96, 96), .1))
        after = np.full((96, 96), .1)
        after[30:60, 30:60] = .4
        self.after = self.make_job('2026-09-13T05:00:00Z', after)
        self.request = dict(site=dict(id=str(uuid.uuid4()), name='Generated test site', kind='Infrastructure', createdAt=0,
                                     region=dict(west=9.0009, south=45.0847, east=9.0153, north=45.0991)),
                            beforeDirectory=str(self.before), afterDirectory=str(self.after),
                            options=dict(polarization='VV', thresholdDb=3, averagingWindow=5, minimumPixels=25))

    def make_job(self, time, array, transform=None):
        directory = self.root/str(uuid.uuid4())
        directory.mkdir()
        record = dict(version=1, state='completed', inputName='GENERATED-FIXTURE.SAFE',
                      metadata=dict(mission='Sentinel-1', satellite='Sentinel-1A', productType='GRD', mode='IW',
                                    acquisitionStart=time, acquisitionEnd=time, orbitDirection='DESCENDING', relativeOrbit=66,
                                    polarizations=['VV'], bands=[], inputName='GENERATED-FIXTURE.SAFE'),
                      options=dict(applyOrbit=True, speckle=False, pixelSpacing=20, polarizations=['VV'], lowPercentile=2, highPercentile=98),
                      processingStages=['Calibration', 'Terrain-Correction'],
                      outputs=[dict(id='VV', scientific='VV-sigma0.tif')])
        save_json(directory/'result.json', record)
        with rasterio.open(directory/'VV-sigma0.tif', 'w', driver='GTiff', width=array.shape[1], height=array.shape[0],
                           count=1, dtype='float32', crs='EPSG:4326', transform=transform or self.transform, nodata=-9999) as dst:
            dst.write(array.astype('float32'), 1)
            dst.update_tags(quantity='Sigma0', units='linear power')
        return directory

    def process(self, request=None):
        target = self.root/str(uuid.uuid4())
        request_file = self.root/'request.json'
        save_json(request_file, request or self.request)
        events = []
        review.run(request_file, target, lambda *e: events.append(e), save_json)
        return target, review.read_json(target/'result.json'), events

    def test_known_power_ratio_and_connected_patch(self):
        target, result, events = self.process()
        with rasterio.open(target/'delta-db.tif') as raster:
            data = raster.read(1, masked=True)
            self.assertAlmostEqual(float(data[40, 40]), 10*np.log10(4), places=5)
            self.assertAlmostEqual(float(data[10, 10]), 0, places=5)
            self.assertEqual(raster.crs.to_epsg(), 4326)
            self.assertEqual(raster.tags()['units'], 'dB')
        self.assertEqual(len(result['candidates']), 1)
        candidate = result['candidates'][0]
        self.assertEqual(candidate['direction'], 'increase')
        self.assertGreater(candidate['pixels'], 800)
        self.assertGreater(candidate['approximateAreaM2'], 0)
        self.assertEqual(result['state'], 'completed')
        self.assertEqual([e[0] for e in events], ['validating', 'processing', 'processing', 'processing', 'exporting', 'completed'])
        self.assertEqual(len(result['evidence'][0]['metadataSHA256']), 64)
        image = np.array(Image.open(target/'Change.png'))
        self.assertEqual(image.shape[2], 4)
        self.assertEqual(image[0, 0, 3], 0)  # Insufficient local support at the edge.

    def test_unchanged_pair_no_candidates(self):
        self.request['afterDirectory'] = str(self.make_job('2026-09-13T05:00:00Z', np.full((96, 96), .1)))
        _, result, _ = self.process()
        self.assertEqual(result['candidates'], [])

    def test_portable_owned_job_retains_source_uuid(self):
        owned = self.root/str(uuid.uuid4())
        owned.mkdir()
        save_json(owned/'run.json', dict(version=1, id=owned.name, operation='process'))
        original = self.before.name.lower()
        self.before.rename(owned/'product')
        self.request['beforeDirectory'] = str(owned/'product')
        _, result, _ = self.process()
        self.assertEqual(result['evidence'][0]['jobID'], owned.name)
        self.assertEqual(result['sourceJobIDs'][0], owned.name)
        self.assertNotEqual(result['sourceJobIDs'][0], 'product')
        self.assertNotEqual(result['sourceJobIDs'][0], original)
        save_json(owned/'run.json', dict(version=1, id=str(uuid.uuid4())))
        with self.assertRaises(review.ReviewError):
            review.source_job_id(owned/'product')

    def test_decrease_and_small_patch_rejection(self):
        data = np.full((96, 96), .1)
        data[25:60, 25:60] = .025
        data[10, 10] = 10
        self.request['afterDirectory'] = str(self.make_job('2026-09-13T05:00:00Z', data))
        self.request['options']['minimumPixels'] = 40
        _, result, _ = self.process()
        self.assertEqual(len(result['candidates']), 1)
        self.assertEqual(result['candidates'][0]['direction'], 'decrease')
        self.assertLess(result['candidates'][0]['meanDeltaDb'], -3)

    def test_invalid_values_never_become_log_ratios(self):
        values = np.full((30, 30), .1)
        for row, value in ((6, 0), (7, -1), (8, np.nan), (9, np.inf)):
            values[row, 15] = value
        a, b, delta = review.delta_db(values, values, 3)
        self.assertTrue(np.isnan(delta[6:10, 15]).all())
        self.assertTrue(np.isnan(a[6:10, 15]).all())
        self.assertAlmostEqual(float(np.nanmax(abs(delta))), 0)
        self.assertFalse(np.isinf(b).any())

    def test_linear_power_is_averaged_before_log(self):
        a = np.ones((15, 15))
        b = a.copy()
        b[7, 7] = 9
        _, _, delta = review.delta_db(a, b, 3)
        self.assertAlmostEqual(float(delta[7, 7]), 10*np.log10(17/9), places=5)

    def test_rejects_incompatible_dates_geometry_settings_and_preview(self):
        first = review.read_json(self.before/'result.json')
        original = review.read_json(self.after/'result.json')
        for key, value in [('relativeOrbit', 67), ('satellite', 'Sentinel-1B'), ('mode', 'EW'), ('orbitDirection', 'ASCENDING'), ('acquisitionStart', first['metadata']['acquisitionStart'])]:
            with self.subTest(key=key):
                last = copy.deepcopy(original)
                last['metadata'][key] = value
                with self.assertRaises(review.ReviewError): review.admit_pair(first, last, 'VV')
        last = copy.deepcopy(original)
        last['options']['speckle'] = True
        with self.assertRaises(review.ReviewError): review.admit_pair(first, last, 'VV')
        with self.assertRaises(review.ReviewError): review.admit_pair(first, original, 'VH')
        last = copy.deepcopy(original)
        last['processingStages'] = []
        with self.assertRaises(review.ReviewError): review.admit_pair(first, last, 'VV')
        with rasterio.open(self.after/'VV-sigma0.tif', 'r+') as dst: dst.update_tags(units='dB')
        with self.assertRaises(review.ReviewError): self.process()

    def test_shifted_grid_alignment_of_constant_power(self):
        # Offset a fraction of a pixel: constant power must remain unchanged.
        self.request['afterDirectory'] = str(self.make_job('2026-09-13T05:00:00Z', np.full((96, 96), .1), from_origin(9+.00005, 45.1+.00004, .00018, .00018)))
        target, result, _ = self.process()
        with rasterio.open(target/'delta-db.tif') as src:
            self.assertLess(float(np.max(abs(src.read(1, masked=True)))), 1e-5)
        self.assertEqual(result['candidates'], [])

    def test_rejects_oversized_site_and_insufficient_coverage(self):
        with rasterio.open(self.before/'VV-sigma0.tif') as src:
            with self.assertRaises(review.ReviewError): review.crop_window(src, [8, 44, 10, 46])
        values = np.zeros((96, 96))
        values[20:25, 20:25] = .1
        self.request['afterDirectory'] = str(self.make_job('2026-09-13T05:00:00Z', values))
        with self.assertRaisesRegex(review.ReviewError, '50%'): self.process()

    def test_failure_removes_partial_outputs_but_preserves_sources(self):
        request_file = self.root/'request.json'
        save_json(request_file, self.request)
        destination = self.root/'failed-review'
        original = (self.before/'VV-sigma0.tif').read_bytes()
        events = []
        with patch.object(review, 'save_preview', side_effect=RuntimeError('deliberate export failure')):
            with self.assertRaises(RuntimeError):
                review.run(request_file, destination, lambda *e: events.append(e), save_json)
        self.assertEqual(review.read_json(destination/'result.json')['state'], 'failed')
        self.assertEqual(events[-1][0], 'failed')
        self.assertEqual(list(destination.glob('*.tif')), [])
        self.assertEqual(list(destination.glob('*.png')), [])
        self.assertTrue((destination/'diagnostics.log').exists())
        self.assertEqual((self.before/'VV-sigma0.tif').read_bytes(), original)
        with self.assertRaisesRegex(review.ReviewError, 'already exists'):
            review.run(request_file, destination, lambda *e: None, save_json)

    def test_safe_raster_path_rejects_traversal_and_symlinks(self):
        for name in ('../secret.tif', '/tmp/a.tif', 'sub/a.tif', 'a\\b.tif'):
            with self.assertRaises(review.ReviewError): review.safe_file(self.before, name)
        (self.before/'escape.tif').symlink_to(self.after/'VV-sigma0.tif')
        with self.assertRaises(review.ReviewError): review.safe_file(self.before, 'escape.tif')

    def test_isolated_worker_cli_end_to_end(self):
        request_file = self.root/'cli-request.json'
        save_json(request_file, self.request)
        destination = self.root/'cli-review'
        child = subprocess.run([sys.executable, '-I', str(WORKER), 'compare', '--input', str(request_file), '--output', str(destination)], capture_output=True, text=True, timeout=30)
        self.assertEqual(child.returncode, 0, child.stdout+child.stderr)
        events = [json.loads(line) for line in child.stdout.splitlines()]
        self.assertEqual(events[0]['state'], 'validating')
        self.assertEqual(events[-1]['state'], 'completed')
        self.assertEqual(review.read_json(destination/'result.json')['state'], 'completed')
        self.assertTrue((destination/'events.jsonl').exists())


if __name__ == '__main__':
    unittest.main()
