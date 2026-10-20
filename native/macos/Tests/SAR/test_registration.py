"""Known shifts and independently controlled failures; not real-product accuracy."""
import copy
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import tempfile
import unittest
import numpy as np
import rasterio
if importlib.util.find_spec("scipy") is None or importlib.util.find_spec("skimage") is None:
    raise unittest.SkipTest("Optional monitoring environment is not installed")
from scipy.ndimage import shift

ROOT=Path(__file__).parents[4]
def module(name,path):
    spec=importlib.util.spec_from_file_location(name,path); m=importlib.util.module_from_spec(spec); spec.loader.exec_module(m); return m
registration=module('registration',ROOT/'native/macos/Sources/GodEyeMac/Resources/SAR/registration.py')
review=module('review',ROOT/'native/macos/Sources/GodEyeMac/Resources/SAR/change_review.py')
fixture=module('fixture',ROOT/'native/desktop/Tests/monitoring_fixture.py')
evaluation=module('evaluation',ROOT/'native/macos/Validation/evaluate_monitoring.py')

class RegistrationTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(); self.addCleanup(self.temp.cleanup); self.root=Path(self.temp.name); self.request=fixture.create(self.root)
    def run_review(self,request=None):
        request=request or self.request; path=self.root/'request.json'; path.write_text(json.dumps(request)); output=self.root/'review'
        events=[]; review.run(path,output,lambda *v:events.append(v),lambda p,v:p.write_text(json.dumps(v,allow_nan=False)))
        self.events=events
        return output,json.loads((output/'result.json').read_text())
    def test_fractional_translation_verification_derivative_and_real_change_preserved(self):
        files=[Path(self.request[k])/'VV-sigma0.tif' for k in ('beforeDirectory','afterDirectory')]; hashes=[hashlib.sha256(f.read_bytes()).hexdigest() for f in files]
        output,r=self.run_review(); self.assertEqual(r['analysisStatus'],'ready'); q=r['quality']; self.assertEqual(q['state'],'passed')
        np.testing.assert_allclose(q['translationPixels'],[-.65,.35],atol=.2); self.assertLessEqual(q['maximumVerificationResidualPixels'],.5)
        self.assertEqual(q['absoluteGeolocation'],'unknown'); self.assertGreater(len(r['candidates']),0)
        self.assertGreater(max(c['meanDeltaDb'] for c in r['candidates']),4)
        self.assertEqual(hashes,[hashlib.sha256(f.read_bytes()).hexdigest() for f in files])
        with rasterio.open(output/'corrected-linear-power.tif') as f: self.assertEqual(f.tags()['units'],'linear power'); self.assertEqual(f.dtypes,('float32',))
        self.assertGreater(q['translationNorthEastMetres'][0],0); self.assertGreater(q['translationNorthEastMetres'][1],0)
    def test_independent_verification_rejects_local_deformation(self):
        f=Path(self.request['afterDirectory'])/'VV-sigma0.tif'
        with rasterio.open(f,'r+') as d:
            values=d.read(1); values[340:460]=shift(values[340:460],(0,1.2),order=1,mode='nearest',prefilter=False); d.write(values,1)
        output,r=self.run_review(); self.assertEqual(r['analysisStatus'],'blocked'); self.assertNotEqual(r['quality']['state'],'passed'); self.assertEqual(r['candidates'],[])
        self.assertEqual([e[0] for e in self.events][-2:],['exporting','completed'])
        self.assertFalse((output/'delta-db.tif').exists()); self.assertTrue((output/'quality.json').exists())
    def test_textureless_and_nodata_controls_do_not_fabricate_alignment(self):
        for kind in ('constant','nodata'):
            with self.subTest(kind=kind):
                with rasterio.open(Path(self.request['beforeDirectory'])/'VV-sigma0.tif','r+') as f:
                    values=f.read(1); values[:150]=.1 if kind=='constant' else -9999; f.write(values,1)
                with rasterio.open(Path(self.request['beforeDirectory'])/'VV-sigma0.tif') as a,rasterio.open(Path(self.request['afterDirectory'])/'VV-sigma0.tif') as b:
                    with self.assertRaises(registration.QualityBlocked): registration.register(a,b,self.request['registration'])
    def test_overlapping_roles_and_insufficient_controls_rejected(self):
        cfg=copy.deepcopy(self.request['registration']); cfg['controls'][3]['region']=cfg['controls'][0]['region']
        with self.assertRaises(ValueError): registration.check_configuration(cfg)
        cfg=copy.deepcopy(self.request['registration']); cfg['controls']=cfg['controls'][:3]
        with self.assertRaises(ValueError): registration.check_configuration(cfg)
    def test_periodic_ambiguous_texture_rejected(self):
        y,x=np.mgrid[:128,:128]; a=np.exp(np.sin(x*np.pi/2)+np.sin(y*np.pi/2))
        with self.assertRaises(ValueError): registration.estimate(a,a,8,.6)
    def test_queued_source_identity_change_is_rejected(self):
        self.request['sourceHashes']=['0'*64,'0'*64]
        with self.assertRaises(review.ReviewError): self.run_review()
        self.assertFalse((self.root/'review'/'corrected-linear-power.tif').exists())
    def test_queued_scientific_file_replacement_is_rejected(self):
        files=[Path(self.request[k])/'VV-sigma0.tif' for k in ('beforeDirectory','afterDirectory')]
        self.request['sourceFileIdentities']=[dict(bytes=f.stat().st_size,modifiedMilliseconds=f.stat().st_mtime_ns//1_000_000) for f in files]
        changed=files[1]; current=changed.stat(); os.utime(changed,ns=(current.st_atime_ns,current.st_mtime_ns+2_000_000))
        with self.assertRaises(review.ReviewError): self.run_review()
        self.assertFalse((self.root/'review'/'corrected-linear-power.tif').exists())
    def test_fits_cannot_be_used_as_verification(self):
        cfg=copy.deepcopy(self.request['registration'])
        for c in cfg['controls']: c['role']='Fit'
        with self.assertRaises(ValueError): registration.check_configuration(cfg)
    def test_evaluation_without_independent_labels_is_pending(self):
        output,_=self.run_review()
        report=evaluation.evaluate(dict(id='generated',split='held-out',labels=[]),output/'result.json')
        self.assertEqual(report['status'],'pending'); self.assertIsNone(report['metrics'])
    def test_labelled_generated_evaluation_has_explicit_false_alarm_and_recall_metrics(self):
        output,_=self.run_review()
        def label(kind,w,s,e,n):
            return dict(**{'class':kind},sourceURL='generated://known-input',referenceDate='2020-07-01',rationale='Generated independent truth mask',geometry=dict(type='Polygon',coordinates=[[[w,s],[e,s],[e,n],[w,n],[w,s]]]))
        case=dict(id='generated',split='held-out',labels=[label('changed',9.0396,45.0478,9.0522,45.0604),label('unchanged',9.03,45.065,9.035,45.07)])
        report=evaluation.evaluate(case,output/'result.json'); self.assertEqual(report['status'],'evaluated'); self.assertGreater(report['metrics']['recall'],.8); self.assertEqual(report['metrics']['falsePositiveRate'],0)
        case['labels'].append(case['labels'][0])
        with self.assertRaises(ValueError): evaluation.evaluate(case,output/'result.json')
if __name__=='__main__': unittest.main()
