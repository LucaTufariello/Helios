"""Independent quality checker numerical regression tests."""
import importlib.util
import datetime as dt
from pathlib import Path
import unittest
import xml.etree.ElementTree as ET
import numpy as np

spec = importlib.util.spec_from_file_location('quality', Path(__file__).parents[1]/'validation/sar_quality.py')
quality = importlib.util.module_from_spec(spec)
spec.loader.exec_module(quality)


class QualityTests(unittest.TestCase):
    def test_linear_db_identity(self):
        result = quality.db_comparison(np.array([.01, .1, 1, 10]), np.array([-20, -10, 0, 10]))
        self.assertEqual(result['validPixels'], 4)
        self.assertEqual(result['maxDbError'], 0)
    def test_wrong_db_is_detected(self):
        self.assertEqual(quality.db_comparison(np.array([.1]), np.array([-7]))['maxDbError'], 3)
    def test_invalid_pixels_require_nodata(self):
        result = quality.db_comparison(np.array([0, -1, np.nan, .1]),
                                       np.ma.array([0, 0, 0, -10], mask=[1, 1, 1, 0]))
        self.assertEqual(result['invalidMaskMismatches'], 0)
        self.assertEqual(result['validPixels'], 1)
        self.assertEqual(quality.db_comparison(np.array([0]), np.array([0]))['invalidMaskMismatches'], 1)
    def test_bilinear_lut_interpolation(self):
        vectors = [ET.fromstring(f'<v><line>{line}</line><pixel>0 10</pixel><sigmaNought>{a} {b}</sigmaNought></v>')
                   for line, a, b in ((0, 1, 3), (10, 5, 7))]
        self.assertEqual(quality.interpolate_sigma(vectors, 5, 5), 4)
        self.assertEqual(quality.interpolate_sigma(vectors, 10, 10), 7)
        with self.assertRaises(ValueError):
            quality.interpolate_sigma(vectors, 11, 5)
    def test_azimuth_time_interpolation(self):
        vectors = [ET.fromstring(f'<v><line>{line}</line><azimuthTime>2026-01-01T00:00:{seconds:02}</azimuthTime><pixel>0 10</pixel><sigmaNought>{a} {b}</sigmaNought></v>')
                   for line, seconds, a, b in ((0, 0, 1, 3), (10, 10, 5, 7))]
        # Time fraction is intentionally different from the line fraction.
        self.assertAlmostEqual(quality.interpolate_sigma(vectors, 5, 5, dt.datetime(2026, 1, 1, 0, 0, 2)), 2.8)
