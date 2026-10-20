#!/usr/bin/env python3
"""Reproducible labelled monitoring evaluation; discovery fetches metadata only."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import urllib.parse
import urllib.request


def read(path):
    path=Path(path)
    if path.stat().st_size>8*1024*1024: raise ValueError('Evaluation document exceeds 8 MiB.')
    return json.loads(path.read_text())


def discover(case):
    b=case['searchBounds']; w,s,e,n=[b[k] for k in ('west','south','east','north')]
    if not all(math.isfinite(v) for v in (w,s,e,n)) or not (-180<=w<e<=180 and -85<=s<n<=85): raise ValueError('Invalid case search region.')
    wkt=f'POLYGON(({w} {s},{e} {s},{e} {n},{w} {n},{w} {s}))'
    inventory=[]
    for window in case['discoveryWindows']:
        query="Collection/Name eq 'SENTINEL-1' and startswith(Name,'S1A_IW_GRDH_1SDV_') and not contains(Name,'_COG') and "
        query+=f"ContentDate/Start ge {window['start']}T00:00:00Z and ContentDate/Start lt {window['endExclusive']}T00:00:00Z and OData.CSC.Intersects(area=geography'SRID=4326;{wkt}')"
        url='https://catalogue.dataspace.copernicus.eu/odata/v1/Products?'+urllib.parse.urlencode({'$filter':query,'$orderby':'ContentDate/Start asc,Id','$top':'100','$expand':'Attributes'})
        req=urllib.request.Request(url,headers={'User-Agent':'GodEye-Evaluation/1.0 (metadata-only)'})
        with urllib.request.urlopen(req,timeout=30) as response:
            data=response.read(8*1024*1024+1)
        if len(data)>8*1024*1024: raise ValueError('Catalogue response exceeds metadata budget.')
        page=json.loads(data)
        records=[]
        for item in page.get('value',[]):
            attributes={v['Name']:v.get('Value') for v in item.get('Attributes',[])}
            records.append(dict(id=item['Id'],name=item['Name'],contentDate=item.get('ContentDate'),bytes=item.get('ContentLength'),online=item.get('Online'),attributes=attributes))
        inventory.append(dict(role=window['role'],query=url,limited=bool(page.get('@odata.nextLink')) or len(records)==100,acquisitions=records))
    return dict(version=1,case=case['id'],status='metadata-discovered',windows=inventory,
                evaluation='pending: select compatible covering acquisitions, process manually and independently label reference imagery before tuning',downloadsPerformed=False)


def evaluate(case,manifest):
    import numpy as np
    import rasterio
    from rasterio.features import rasterize
    from rasterio.crs import CRS
    result=read(manifest)
    report=dict(version=1,case=case['id'],split=case['split'],status='pending',metrics=None)
    if result.get('analysisStatus')!='ready' or result.get('quality',{}).get('state')!='passed':
        return {**report,'status':'blocked','reason':'A corrected, independently verified monitoring result is required.'}
    labels=case.get('labels',[])
    if not labels: return {**report,'reason':'Independent changed/unchanged reference labels have not been authored; no accuracy claim.'}
    if case['split'] not in ('development','held-out'): raise ValueError('Declare development or held-out before evaluation.')
    shapes=[]
    for label in labels:
        if label['class'] not in ('changed','unchanged','uncertain') or not label.get('sourceURL') or not label.get('referenceDate') or not label.get('rationale'):
            raise ValueError('Every reference label needs its class, dated independent source and rationale.')
        shapes.append((label['geometry'],{'changed':1,'unchanged':2,'uncertain':3}[label['class']]))
    root=Path(manifest).resolve().parent
    paths=[root/name for name in ('candidate-mask.tif','delta-db.tif')]
    if any(p.is_symlink() or not p.is_file() or p.resolve().parent!=root for p in paths): raise ValueError('Completed scientific outputs must be regular local files.')
    with rasterio.open(paths[0]) as candidate,rasterio.open(paths[1]) as delta:
        if candidate.crs!=CRS.from_epsg(4326) or delta.crs!=candidate.crs or candidate.transform!=delta.transform or candidate.shape!=delta.shape or candidate.width*candidate.height>1_000_000:
            raise ValueError('Scientific evaluation grids are incompatible or exceed the bounded review cap.')
        truth=np.zeros(candidate.shape,dtype='uint8')
        for geometry,value in shapes:
            patch=rasterize([(geometry,value)],out_shape=candidate.shape,transform=candidate.transform,fill=0)
            if np.any((patch>0)&(truth>0)): raise ValueError('Reference labels overlap on the analysis grid.')
            truth[patch>0]=patch[patch>0]
        measured=candidate.read(1); scientific=delta.read(1,masked=True); valid=~np.ma.getmaskarray(scientific)&np.isfinite(scientific.filled(np.nan))
        positive=measured>0; changed=(truth==1)&valid; unchanged=(truth==2)&valid
        tp,fn,fp,tn=[int(v) for v in ((positive&changed).sum(),((~positive)&changed).sum(),(positive&unchanged).sum(),((~positive)&unchanged).sum())]
        ratio=lambda a,b:a/b if b else None
        metrics=dict(truePositivePixels=tp,falseNegativePixels=fn,falsePositivePixels=fp,trueNegativePixels=tn,precision=ratio(tp,tp+fp),recall=ratio(tp,tp+fn),falsePositiveRate=ratio(fp,fp+tn),uncertainOrUnlabelledPixels=int(((truth==0)|(truth==3)).sum()),invalidLabelledPixels=int(((truth>0)&(~valid)).sum()))
    if tp+fn==0 or fp+tn==0: return {**report,'reason':'Both changed and unchanged independently labelled valid pixels are required.','labelCounts':metrics}
    return {**report,'status':'evaluated','metrics':metrics,'registration':result['quality'],
            'sourceMetadataSHA256':result.get('sourceMetadataSHA256'),
            'referenceLabelsSHA256':hashlib.sha256(json.dumps(labels,sort_keys=True).encode()).hexdigest(),
            'limitations':['Pixel-level results apply only to the declared case and acquisition split.','Reference labels must be independently authored before tuning.','A radar-change candidate does not identify a physical cause or establish operational status.']}


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--case',required=True,type=Path);p.add_argument('--review',type=Path);p.add_argument('--discover',action='store_true');p.add_argument('--report',required=True,type=Path);args=p.parse_args()
    case=read(args.case)
    if case.get('version')!=1: raise ValueError('Unsupported evaluation protocol.')
    report=discover(case) if args.discover else evaluate(case,args.review) if args.review else dict(version=1,case=case['id'],status='pending',metrics=None,reason='No completed scientific review supplied.')
    args.report.parent.mkdir(parents=True,exist_ok=True);args.report.write_text(json.dumps(report,indent=2,allow_nan=False)+'\n')
    print(report['status']);return 0 if report['status'] in ('evaluated','metadata-discovered') else 2
if __name__=='__main__':raise SystemExit(main())
