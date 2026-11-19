"""Additional optical viewers. Display stretches never replace source values.
CBERS L4 is already orthorectified; PRISMA admission requires geocoded L2D.
No unrectified image is assigned a guessed CRS. No pansharpening is applied.
"""
import datetime as dt
import math
from pathlib import Path
import re
from contextlib import ExitStack

import numpy as np
import rasterio
from rasterio.enums import Resampling, ColorInterp
from rasterio.transform import from_bounds, from_origin
from rasterio.vrt import WarpedVRT
from rasterio.warp import transform_bounds
from rasterio.windows import Window, from_bounds as bounds_window
from PIL import Image

MAX_SIDE = 2048


class SourceError(Exception):
    pass


def cbers_asset(identifier, band):
    if not re.fullmatch(r'CBERS_4A_WPM_\d{8}_\d{3}_\d{3}_L4', identifier) or band not in ('BAND0','BAND1','BAND2','BAND3','BAND4'):
        raise SourceError('Unsupported CBERS-4A WPM L4 product or band.')
    parts = identifier.split('_')
    return f'https://brazil-eosats.s3.us-west-2.amazonaws.com/CBERS4A/WPM/{parts[4]}/{parts[5]}/{identifier}/{identifier}_{band}.tif'


def validate_cbers(scene, mode):
    if mode not in ('pan', 'rgb'):
        raise SourceError('Choose CBERS panchromatic or native RGB display.')
    dt.datetime.fromisoformat(scene['acquiredAt'].replace('Z','+00:00'))
    for key in ('BAND0','BAND1','BAND2','BAND3'):
        if scene.get('bands',{}).get(key) != cbers_asset(scene['id'],key):
            raise SourceError('CBERS image URL is outside the trusted public COG archive.')


def output_grid(dataset, bounds):
    if not dataset.crs or not dataset.crs.is_projected or min(dataset.res) <= 0:
        raise SourceError('A projected, georeferenced raster is required.')
    footprint = transform_bounds(dataset.crs,'EPSG:4326',*dataset.bounds,densify_pts=21)
    clipped = [max(bounds[0],footprint[0]),max(bounds[1],footprint[1]),min(bounds[2],footprint[2]),min(bounds[3],footprint[3])]
    if clipped[0] >= clipped[2] or clipped[1] >= clipped[3]:
        raise SourceError('The product does not cover the selected region.')
    projected = transform_bounds('EPSG:4326',dataset.crs,*clipped,densify_pts=21)
    cols, rows = [max(1,math.ceil(length/res)) for length,res in zip((projected[2]-projected[0],projected[3]-projected[1]),dataset.res)]
    scale = max(1,cols/MAX_SIDE,rows/MAX_SIDE)
    width,height = min(MAX_SIDE,math.ceil(cols/scale)),min(MAX_SIDE,math.ceil(rows/scale))
    return clipped,width,height,from_bounds(*clipped,width,height),Resampling.nearest if scale == 1 else Resampling.average


def stretch(values, valid):
    """Separate 2/98 percentile display clips per selected channel, not reflectance."""
    rendered = []
    ranges = []
    for channel in values:
        low,high = np.percentile(channel[valid], [2,98]).astype(float)
        if high <= low:
            high = low+max(abs(low)*.01,1e-6)
        rendered.append((np.clip((channel-low)/(high-low),0,1)*255).astype('uint8'))
        ranges.append([float(low),float(high)])
    if len(rendered) == 1:
        rendered *= 3
    rgba = np.stack(rendered+[valid.astype('uint8')*255])
    rgba[:3,~valid] = 0
    return rgba,ranges


def write_outputs(values, valid, grid, destination, quantity, units, emit, scientific=True):
    bounds,width,height,transform,resampling = grid
    coverage = float(valid.mean())
    if not coverage:
        raise SourceError('No valid source pixels in the selected region.')
    emit('exporting','Saving optical display and source-value GeoTIFFs',.9)
    rgba,ranges = stretch(values,valid)
    Image.fromarray(np.moveaxis(rgba,0,-1)).save(destination/'preview.png')
    profile = dict(driver='GTiff',width=width,height=height,crs='EPSG:4326',transform=transform,
                   tiled=True,blockxsize=256,blockysize=256,compress='deflate')
    with rasterio.open(destination/'true-color.tif','w',**profile,count=4,dtype='uint8') as target:
        target.write(rgba)
        target.colorinterp = (ColorInterp.red,ColorInterp.green,ColorInterp.blue,ColorInterp.alpha)
        target.update_tags(quantity=quantity,units='8-bit display only',percentiles='2,98',ranges=str(ranges))
    if scientific:
        with rasterio.open(destination/'source-values.tif','w',**profile,count=len(values),dtype='float32',nodata=float('nan')) as target:
            target.write(np.where(valid,values,np.nan).astype('float32'))
            target.update_tags(quantity=quantity,units=units,resampling=resampling.name)
    return dict(bounds=bounds,width=width,height=height,crs='EPSG:4326',validFraction=coverage,
                preview='preview.png',geotiff='true-color.tif',scientific='source-values.tif' if scientific else None,
                displayRanges=ranges,resampling=resampling.name)


def render_cbers(scene,bounds,destination,mode,emit):
    validate_cbers(scene,mode)
    keys = ['BAND0'] if mode == 'pan' else ['BAND3','BAND2','BAND1']
    emit('processing','Reading CBERS orthorectified COG windows',None)
    with ExitStack() as stack:
        sources = [stack.enter_context(rasterio.open(scene['bands'][key],driver='GTiff')) for key in keys]
        for source in sources:
            if source.count != 1 or source.dtypes[0] not in ('uint8','uint16','int16'):
                raise SourceError('Expected CBERS single-band digital-number GeoTIFFs.')
            if not source.crs or not source.crs.is_projected or any(abs(r-(2 if mode=='pan' else 8))>.1 for r in source.res):
                raise SourceError('CBERS WPM source spacing does not match the selected sensor mode.')
        grid = output_grid(sources[0],bounds)
        _,width,height,transform,resampling = grid
        values, masks = [], []
        for source in sources:
            with WarpedVRT(source,crs='EPSG:4326',transform=transform,width=width,height=height,
                           nodata=0,resampling=resampling,warp_mem_limit=64) as view:
                channel = view.read(1,out_dtype='float32')
                masks.append((view.read_masks(1)>0)&np.isfinite(channel)&(channel>0))
                values.append(channel)
        values = np.stack(values)
        valid = np.logical_and.reduce(masks)
        result = write_outputs(values,valid,grid,destination,'CBERS-4A WPM L4 '+mode,
                               'sensor digital numbers; not surface reflectance',emit)
        result.update(sourceResolutionMetres=list(sources[0].res),mode=mode,
                      limitations=['Dated image; public CBERS coverage is regional, not global.',
                                   '2 m PAN is grayscale; native RGB is 8 m. No pansharpening.',
                                   'Source values are digital numbers, not calibrated surface reflectance.',
                                   'Cloud cover may be unknown; clouds/shadows are not masked.',
                                   '2/98 percentile stretch is local to this region; colours are display values.',
                                   'Display and selected-band exports are bounded to 2048 pixels per side.'])
        for name in ('true-color.tif','source-values.tif'):
            with rasterio.open(destination/name,'r+') as output:
                output.update_tags(source_id=scene['id'],acquiredAt=scene['acquiredAt'],bands=','.join(keys))
        return result


def hard_dataset(hdf, path, mask=False):
    """Only hard links: reject HDF external/soft links and external/virtual data."""
    import h5py
    node = hdf
    for part in path.split('/'):
        if not isinstance(node.get(part,getlink=True),h5py.HardLink):
            raise SourceError('PRISMA external or soft HDF links are unsupported.')
        node = node[part]
    if not isinstance(node,h5py.Dataset) or node.is_virtual or node.external:
        raise SourceError('PRISMA must contain embedded, non-virtual pixel datasets.')
    if node.chunks and math.prod(node.chunks)*node.dtype.itemsize > 128*1024*1024:
        raise SourceError('PRISMA compressed chunks exceed the safe memory limit.')
    if node.dtype not in ([np.dtype('uint8'),np.dtype('uint16')] if mask else [np.dtype('uint16')]) or not 2 <= node.ndim <= 3:
        raise SourceError('Expected unsigned 16-bit PRISMA L2D pixel data.')
    return node


def prisma_geometry(metadata,width,height,resolution):
    from rasterio.crs import CRS
    def scalar(name):
        if name not in metadata:
            raise SourceError('Missing PRISMA projection metadata: '+name)
        value = np.asarray(metadata[name]).reshape(-1)
        if value.size != 1 or not np.isfinite(float(value[0])):
            raise SourceError('Missing or invalid PRISMA projection metadata.')
        return float(value[0])
    epsg = int(scalar('Epsg_Code'))
    if not (32601 <= epsg <= 32660 or 32701 <= epsg <= 32760):
        raise SourceError('PRISMA L2D must supply a supported WGS84 UTM CRS.')
    west,north,east,south = [scalar(k) for k in ('Product_ULcorner_easting','Product_ULcorner_northing','Product_LRcorner_easting','Product_LRcorner_northing')]
    # PRISMA L2D corners refer to pixel centres (EnMAP-Box L2D reader).
    # Refuse inconsistent dimensions rather than assigning approximate bounds.
    if width < 2 or height < 2:
        raise SourceError('PRISMA geocoded dimensions must exceed one pixel.')
    dx,dy = (east-west)/(width-1),(north-south)/(height-1)
    if not all(math.isfinite(v) and abs(v-resolution) < .1 for v in (dx,dy)):
        raise SourceError('PRISMA corner coordinates and raster dimensions disagree. This product cannot be safely georeferenced.')
    # Preserve the supplied centre-to-centre extent, including PAN/HCO grid offsets.
    return CRS.from_epsg(epsg),from_origin(west-dx/2,north+dy/2,dx,dy)


def render_prisma(request,destination,emit):
    try:
        import h5py
    except ImportError:
        raise SourceError('PRISMA import requires h5py in the configured Python raster environment. Run native/workers/setup-raster.sh.')
    path = Path(request['originalInput'])
    if not path.is_absolute() or path.is_symlink() or not path.is_file() or not re.fullmatch(r'PRS_L2D_[A-Za-z0-9_]+\.he5',path.name):
        raise SourceError('Select an extracted PRISMA L2D .he5 product, not L1/L2B/L2C or ZIP.')
    mode = request.get('mode','pan')
    if mode not in ('pan','rgb','spectral'):
        raise SourceError('Unsupported PRISMA display mode.')
    emit('processing','Inspecting PRISMA L2D projection and embedded bands',.08)
    with h5py.File(path,'r') as hdf:
        swath = 'PRS_L2D_PCO' if mode == 'pan' else 'PRS_L2D_HCO'
        prefix = f'HDFEOS/SWATHS/{swath}/Data Fields/'
        wavelengths = []
        if mode == 'pan':
            cube = hard_dataset(hdf,prefix+'Cube')
            if cube.ndim != 2:
                raise SourceError('Unsupported PRISMA PAN dimensions.')
            height,width = cube.shape
            indices = [None]
            errors = hard_dataset(hdf,prefix+'PIXEL_L2_ERR_MATRIX',mask=True)
            resolution = 5
        else:
            target = float(request.get('wavelengthNanometres',660))
            if not math.isfinite(target) or not 400 <= target <= 2500:
                raise SourceError('Choose a spectral wavelength from 400 to 2500 nm.')
            detector = 'SWIR' if mode == 'spectral' and target > 1000 else 'VNIR'
            cube = hard_dataset(hdf,prefix+detector+'_Cube')
            if cube.ndim != 3:
                raise SourceError('Unsupported PRISMA spectral cube dimensions.')
            height,band_count,width = cube.shape
            cw = np.asarray(hdf.attrs['List_Cw_'+detector.title()],dtype=float).reshape(-1)
            if len(cw) != band_count or not 1 <= band_count <= 256 or not np.all(np.isfinite(cw)):
                raise SourceError('PRISMA wavelength metadata does not match its cube.')
            flags_name = 'List_Cw_'+detector.title()+'_Flags'
            band_valid = cw > 0
            if flags_name in hdf.attrs:
                flags = np.asarray(hdf.attrs[flags_name]).reshape(-1)
                if len(flags) != band_count:
                    raise SourceError('PRISMA wavelength flags do not match its cube.')
                band_valid &= flags == 1
            targets = [660,550,480] if mode == 'rgb' else [target]
            indices = []
            for wave in targets:
                distance = np.where(band_valid,abs(cw-wave),np.inf)
                index = int(np.argmin(distance))
                if distance[index] > 30:
                    raise SourceError('No valid PRISMA band near the requested wavelength.')
                indices.append(index);wavelengths.append(float(cw[index]))
            errors = hard_dataset(hdf,prefix+detector+'_PIXEL_L2_ERR_MATRIX',mask=True)
            resolution = 30
        if errors.shape != cube.shape or not 1 <= width <= 20000 or not 1 <= height <= 20000:
            raise SourceError('PRISMA error masks or dimensions are inconsistent.')
        detector_name = 'Pan' if mode == 'pan' else detector.title()
        try:
            minimum = float(np.asarray(hdf.attrs['L2Scale'+detector_name+'Min']).reshape(-1)[0])
            maximum = float(np.asarray(hdf.attrs['L2Scale'+detector_name+'Max']).reshape(-1)[0])
        except (KeyError,ValueError,IndexError):
            raise SourceError('PRISMA L2D reflectance scaling metadata is required.')
        if not math.isfinite(minimum) or not math.isfinite(maximum) or maximum <= minimum:
            raise SourceError('Invalid PRISMA reflectance scaling metadata.')
        crs,transform = prisma_geometry(hdf.attrs,width,height,resolution)
        whole_bounds = rasterio.transform.array_bounds(height,width,transform)
        footprint = list(transform_bounds(crs,'EPSG:4326',*whole_bounds,densify_pts=21))
        bounds = request.get('region')
        requested = [float(bounds[k]) for k in ('west','south','east','north')] if bounds else footprint
        if not all(math.isfinite(x) for x in requested) or not (-180 <= requested[0] < requested[2] <= 180 and -85 <= requested[1] < requested[3] <= 85):
            raise SourceError('Invalid PRISMA viewing region.')
        projected = transform_bounds('EPSG:4326',crs,*requested,densify_pts=21)
        crop = bounds_window(*projected,transform=transform)
        x0,y0 = max(0,math.floor(crop.col_off)),max(0,math.floor(crop.row_off))
        x1,y1 = min(width,math.ceil(crop.col_off+crop.width)),min(height,math.ceil(crop.row_off+crop.height))
        if x1 <= x0 or y1 <= y0:
            raise SourceError('The PRISMA product does not cover the selected region.')
        # Read selected bands only in row windows. Never load the full HS cube.
        temporary = destination/'selected.tmp.tif'
        try:
            with rasterio.open(temporary,'w',driver='GTiff',width=x1-x0,height=y1-y0,count=len(indices),dtype='float32',
                               crs=crs,transform=rasterio.windows.transform(Window(x0,y0,x1-x0,y1-y0),transform),nodata=float('nan'),
                               tiled=True,blockxsize=256,blockysize=256,compress='deflate') as out:
                for row in range(y0,y1,64):
                    stop = min(row+64,y1)
                    for band,index in enumerate(indices,1):
                        selection = (slice(row,stop),slice(x0,x1)) if index is None else (slice(row,stop),index,slice(x0,x1))
                        raw = cube[selection]
                        flags = errors[selection]
                        # L2D unitless surface reflectance uses each product's Min/Max metadata.
                        valid = (raw>0)&(flags==0)
                        out.write(np.where(valid,minimum+raw.astype('float32')*((maximum-minimum)/65535),np.nan),band,
                                  window=Window(0,row-y0,x1-x0,stop-row))
                    emit('processing','Reading selected PRISMA bands and quality masks',.1+.65*(stop-y0)/(y1-y0))
            with rasterio.open(temporary) as dataset:
                grid = output_grid(dataset,requested)
                _,w,h,tr,resampling = grid
                with WarpedVRT(dataset,crs='EPSG:4326',transform=tr,width=w,height=h,resampling=resampling,
                               nodata=float('nan'),warp_mem_limit=64) as view:
                    values = view.read(out_dtype='float32')
                valid = np.all(np.isfinite(values),axis=0)
                values = np.where(valid,values,0)
                result = write_outputs(values,valid,grid,destination,'PRISMA L2D '+mode,'unitless surface reflectance',emit)
        finally:
            temporary.unlink(missing_ok=True)
        stamp = re.search(r'(\d{14})',path.stem)
        if not stamp:
            raise SourceError('PRISMA product filename lacks an acquisition timestamp.')
        acquired = dt.datetime.strptime(stamp.group(1),'%Y%m%d%H%M%S').replace(tzinfo=dt.timezone.utc).isoformat()
        # The acquisition timestamp in the official filename is retained when
        # Product_StartTime is absent. Prefer product metadata when available.
        if 'Product_StartTime' in hdf.attrs:
            product_time = np.asarray(hdf.attrs['Product_StartTime']).reshape(-1)[0]
            if isinstance(product_time,bytes): product_time = product_time.decode('utf-8')
            date = dt.datetime.fromisoformat(str(product_time).replace('Z','+00:00'))
            if date.tzinfo is None: date = date.replace(tzinfo=dt.timezone.utc)
            acquired = date.isoformat()
        cloud = None
        if 'Cloudy_pixels_percentage' in hdf.attrs:
            value = float(np.asarray(hdf.attrs['Cloudy_pixels_percentage']).reshape(-1)[0])
            if math.isfinite(value) and 0 <= value <= 100: cloud = value
        scene = dict(id=path.stem,acquiredAt=acquired,platform='PRISMA',cloudCover=cloud,bounds=footprint,visualURL='',source='prisma')
        for name in ('true-color.tif','source-values.tif'):
            with rasterio.open(destination/name,'r+') as output:
                output.update_tags(source_id=scene['id'],acquiredAt=acquired,
                                   reflectanceScaleMin=minimum,reflectanceScaleMax=maximum,
                                   reflectanceFormula='ScaleMin + DN * (ScaleMax - ScaleMin) / 65535',
                                   wavelengthsNanometres=str(wavelengths))
                if name == 'source-values.tif':
                    for index in range(1,output.count+1):
                        output.set_band_description(index,'PAN surface reflectance' if mode=='pan' else f'{wavelengths[index-1]:.2f} nm surface reflectance')
        region = dict(zip(('west','south','east','north'),requested))
        result.update(reflectanceScale=[minimum,maximum], scene=scene,region=region,originalInput=str(path),mode=mode,sourceResolutionMetres=[resolution,resolution],
                      wavelengthsNanometres=wavelengths,
                      limitations=['ASI registration and an extracted geocoded L2D HE5 product are required; no anonymous product download.',
                                   '5 m PAN is grayscale; VNIR RGB and selected spectral bands have 30 m pixels. No pansharpening.',
                                   'Only selected bands are exported; the original hyperspectral cube remains unchanged.',
                                   'Quality-error pixels and raw zeros are masked; clouds/shadows are not automatically masked.',
                                   'Surface reflectance uses the product L2Scale Min/Max; independent radiometric/geographic validation remains pending.',
                                   'Selected-band map products are bounded to 2048 pixels per side.'])
        return result
