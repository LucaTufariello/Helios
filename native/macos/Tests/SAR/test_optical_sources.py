"""Generated fixtures verify import/math/geometry/security, not sensor accuracy."""
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import h5py
import numpy as np
import rasterio
from rasterio.transform import from_origin
from rasterio.warp import transform_bounds

WORKER = Path(__file__).parents[2]/'Sources/HeliosMac/Resources/Optical/sentinel2.py'
spec = importlib.util.spec_from_file_location('optical',WORKER)
optical = importlib.util.module_from_spec(spec);spec.loader.exec_module(optical)
extra = optical.extra_sources()


class OpticalSourcesTests(unittest.TestCase):
    def setUp(self):
        tmp = tempfile.TemporaryDirectory();self.addCleanup(tmp.cleanup)
        self.root = Path(tmp.name)
        self.path = self.root/'PRS_L2D_STD_20260922102800_20260922102804_0001.he5'
        self.out = self.root/'out';self.out.mkdir()

    def fixture(self,mode='pan'):
        size,res = (72,5) if mode=='pan' else (12,30)
        with h5py.File(self.path,'w') as hdf:
            hdf.attrs.update(Epsg_Code=32632,Product_ULcorner_easting=500000.,Product_ULcorner_northing=5050000.,
                             Product_LRcorner_easting=500000.+(size-1)*res,Product_LRcorner_northing=5050000.-(size-1)*res,
                             L2ScalePanMin=.1,L2ScalePanMax=.9,L2ScaleVnirMin=.05,L2ScaleVnirMax=.8,
                             L2ScaleSwirMin=.2,L2ScaleSwirMax=.95)
            if mode=='pan':
                group=hdf.create_group('HDFEOS/SWATHS/PRS_L2D_PCO/Data Fields')
                group['Cube']=np.full((size,size),32768,dtype='uint16')
                flags=np.zeros((size,size),dtype='uint8');flags[:5,:5]=1
                group['PIXEL_L2_ERR_MATRIX']=flags
            else:
                group=hdf.create_group('HDFEOS/SWATHS/PRS_L2D_HCO/Data Fields')
                hdf.attrs['List_Cw_Vnir']=[660,550,480,0]
                hdf.attrs['List_Cw_Vnir_Flags']=[1,1,1,0]
                hdf.attrs['List_Cw_Swir']=[1100,1600,2200]
                group['VNIR_Cube']=np.stack([np.full((size,size),i,dtype='uint16') for i in (10000,20000,30000,0)],axis=1)
                group['VNIR_PIXEL_L2_ERR_MATRIX']=np.zeros((size,4,size),dtype='uint8')
                group['SWIR_Cube']=np.full((size,3,size),40000,dtype='uint16')
                group['SWIR_PIXEL_L2_ERR_MATRIX']=np.zeros((size,3,size),dtype='uint8')
        return dict(source='prisma',originalInput=str(self.path),mode=mode)

    def test_prisma_pan_scaled_using_product_metadata_not_arbitrary_contrast(self):
        result=extra.render_prisma(self.fixture(),self.out,lambda *args:None)
        with rasterio.open(self.out/'source-values.tif') as source:
            valid=source.read(1,masked=True).compressed()
            self.assertTrue(np.allclose(valid,.1+32768*.8/65535,atol=1e-6))
            self.assertEqual(source.crs.to_epsg(),4326)
            self.assertTrue(np.isnan(source.read(1)).any())
            self.assertIn('reflectance',source.tags()['units'])
        self.assertEqual(result['sourceResolutionMetres'],[5,5])
        self.assertEqual(result['scene']['source'],'prisma')
        self.assertEqual(result['reflectanceScale'],[.1,.9])
        self.assertFalse((self.out/'selected.tmp.tif').exists())
        self.assertTrue(self.path.exists())

    def test_prisma_rgb_native_spectral_resolution_and_wavelength_order(self):
        result=extra.render_prisma(self.fixture('rgb'),self.out,lambda *args:None)
        self.assertEqual(result['wavelengthsNanometres'],[660,550,480])
        self.assertEqual(result['sourceResolutionMetres'],[30,30])
        with rasterio.open(self.out/'source-values.tif') as source:
            self.assertEqual(source.count,3)
            means=[np.nanmean(x) for x in source.read()]
        self.assertTrue(np.allclose(means,np.array([10000,20000,30000])*.75/65535+.05,atol=1e-6))

    def test_prisma_swir_selection_and_bad_band_rejection(self):
        request=self.fixture('rgb');request.update(mode='spectral',wavelengthNanometres=1610)
        result=extra.render_prisma(request,self.out,lambda *args:None)
        self.assertEqual(result['wavelengthsNanometres'],[1600])
        request['wavelengthNanometres']=2000
        with self.assertRaises(extra.SourceError):extra.render_prisma(request,self.out,lambda *args:None)

    def test_prisma_missing_scale_or_inconsistent_crs_never_gets_fake_geometry(self):
        request=self.fixture()
        with h5py.File(self.path,'a') as hdf:del hdf.attrs['L2ScalePanMax']
        with self.assertRaises(extra.SourceError):extra.render_prisma(request,self.out,lambda *args:None)
        request=self.fixture()
        with h5py.File(self.path,'a') as hdf:hdf.attrs['Product_LRcorner_easting']=510000.
        with self.assertRaises(extra.SourceError):extra.render_prisma(request,self.out,lambda *args:None)
        self.assertFalse((self.out/'source-values.tif').exists())

    def test_prisma_hdf_external_and_soft_links_rejected(self):
        request=self.fixture()
        with h5py.File(self.path,'a') as hdf:
            group=hdf['HDFEOS/SWATHS/PRS_L2D_PCO/Data Fields'];del group['Cube'];group['Cube']=h5py.ExternalLink('/etc/passwd','/x')
        with self.assertRaises(extra.SourceError):extra.render_prisma(request,self.out,lambda *args:None)
        with h5py.File(self.path,'a') as hdf:
            group=hdf['HDFEOS/SWATHS/PRS_L2D_PCO/Data Fields'];del group['Cube'];group['Cube']=h5py.SoftLink('/whatever')
        with self.assertRaises(extra.SourceError):extra.render_prisma(request,self.out,lambda *args:None)

    def test_prisma_selected_region_clipped_and_not_full_product(self):
        request=self.fixture()
        bounds=transform_bounds('EPSG:32632','EPSG:4326',500050,5049750,500200,5049900)
        request['region']=dict(zip(('west','south','east','north'),bounds))
        result=extra.render_prisma(request,self.out,lambda *args:None)
        self.assertLess(result['width'],40);self.assertLess(result['height'],40)
        self.assertTrue(np.allclose(result['bounds'],bounds))

    def test_prisma_geocoding_preserves_pixel_centres(self):
        metadata=dict(Epsg_Code=32632,Product_ULcorner_easting=500000,Product_ULcorner_northing=5050000,
                      Product_LRcorner_easting=500355,Product_LRcorner_northing=5049645)
        _,transform=extra.prisma_geometry(metadata,72,72,5)
        self.assertEqual(transform*(.5,.5),(500000,5050000))
        self.assertEqual(transform*(71.5,71.5),(500355,5049645))

    def test_failure_cleanup_retains_original_and_diagnostics(self):
        request=self.fixture();file=self.root/'request.json';file.write_text(json.dumps(request))
        with h5py.File(self.path,'a') as hdf:del hdf.attrs['Epsg_Code']
        target=self.root/'failed'
        with self.assertRaises(Exception):optical.run(file,target)
        result=json.loads((target/'result.json').read_text())
        self.assertEqual(result['state'],'failed')
        self.assertFalse((target/'preview.png').exists());self.assertTrue(self.path.exists())
        self.assertTrue((target/'diagnostics.log').exists())

    def test_cbers_admission_and_native_pan_not_invented_rgb(self):
        identifier='CBERS_4A_WPM_20261003_198_130_L4'
        scene=dict(id=identifier,acquiredAt='2026-10-03T12:43:23Z',source='cbers4a',
                   bands={f'BAND{i}':extra.cbers_asset(identifier,f'BAND{i}') for i in range(5)})
        file=self.root/'pan.tif'
        with rasterio.open(file,'w',driver='GTiff',count=1,width=64,height=64,dtype='int16',nodata=0,
                           crs='EPSG:32724',transform=from_origin(500000,8500000,2,2)) as target:
            target.write(np.arange(4096,dtype='int16').reshape(64,64),1)
            bounds=transform_bounds(target.crs,'EPSG:4326',*target.bounds)
        original=rasterio.open
        with patch.object(rasterio,'open',side_effect=lambda path,*a,**k:original(file,*a,**k) if str(path).startswith('https:') else original(path,*a,**k)):
            result=extra.render_cbers(scene,bounds,self.out,'pan',lambda *args:None)
        self.assertEqual(result['sourceResolutionMetres'],[2,2])
        with rasterio.open(self.out/'true-color.tif') as display:
            rgb=display.read();self.assertTrue(np.array_equal(rgb[0],rgb[1]))
        scene['bands']['BAND0']='file:///etc/passwd'
        with self.assertRaises(extra.SourceError):extra.validate_cbers(scene,'pan')

    def test_constant_stretch_finite_and_invalid_transparent(self):
        values=np.full((1,20,20),.5,dtype='float32');mask=np.ones((20,20),bool);mask[0,0]=False
        rgba,ranges=extra.stretch(values,mask)
        self.assertEqual(rgba[3,0,0],0);self.assertEqual(rgba[3,1,1],255)
        self.assertTrue(np.isfinite(ranges).all())
