#!/usr/bin/env python3
'''Independent h5py verification of actual production HTTP renderer point traces.'''
import argparse, hashlib, json, struct
from pathlib import Path
import h5py
import numpy as np
def bits(v):return struct.pack('<d',float(v)).hex()
def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--project-root',required=True,type=Path)
    ap.add_argument('--trace',required=True,type=Path)
    ap.add_argument('--output',required=True,type=Path)
    args=ap.parse_args();root=args.project_root.resolve()
    trace=json.loads(args.trace.read_text());rows=[]
    for row in trace['records']:
        request=row['pointTrace']['request'];result=row['pointTrace']['response']['result']
        path=(root/request['relativePath']).resolve();assert path.is_relative_to(root)
        before=hashlib.sha256(path.read_bytes()).hexdigest()
        assert before==row['fileSha256']==request['expectedFileSha256']==result['file']['sha256']
        p=result['payload'];native=p['nativeCells'];point=request['pointQuery']['point']
        field=request['pointQuery']['field'];assert field=='DENS'
        with h5py.File(path,'r') as f:
            d=int(f.attrs['dim']);assert d in (1,2) and f.attrs['geometry']=='cartesian'
            shape=f['Data/'+field].shape;total=int(np.prod(shape))
            matched=np.ones(total,dtype=bool)
            for axis,x in enumerate(point,1):
                lo=f['NativeGrid/x'+str(axis)+'_lower'][:].reshape(-1)
                hi=f['NativeGrid/x'+str(axis)+'_upper'][:].reshape(-1)
                matched &= (x>=lo)&((x<hi)|((x==hi)&(hi==hi.max())))
            hits=np.flatnonzero(matched);assert len(hits)==1
            index=int(hits[0]);assert p['linearIndices']==[index]
            per=int(np.prod(shape[1:]));block=index//per
            local=list(np.unravel_index(index%per,shape[1:]))
            assert p['block']==block and p['start']==[int(v) for v in local]
            ds=f['Data/'+field];assert ds.dtype.kind=='f' and ds.dtype.itemsize==8
            value=float(ds[tuple([block]+local)])
            assert bits(value)==bits(p['values'][0])
            measure=bits(f['NativeGrid/cell_measure'][index]);assert measure==bits(native['cellMeasure'][0])
            for axis in ['x1','x2','x3']:
                for edge in ['lower','upper']:
                    assert bits(f['NativeGrid/'+axis+'_'+edge][index])==bits(native[edge][axis][0])
            for axis in ['x','y','z']:
                assert bits(f['Grid/'+axis][index])==bits(p['coordinates'][axis][0])
            level=int(f['Grid/level'][block])
            key='/'.join(map(str,[level]+[int(f['NativeGrid/logical_'+axis][block]) for axis in ['x1','x2','x3']]))
            assert native['level']==level and native['logicalKey']==key
            assert result['pointEvidence']['point']==point and result['pointEvidence']['matchCount']==1
            assert result['pointEvidence']['scannedCells']==total
            assert p['unit']==str(ds.attrs['unit'])
            assert before in row['pointInspector'] and key in row['pointInspector']
            producer={k:str(f['SourceIdentity'].attrs[k]) for k in ['case_id','raw_config_sha256','binary_sha256','run_id']}
            assert producer['case_id']==row['caseId']
        assert hashlib.sha256(path.read_bytes()).hexdigest()==before
        assert row['errorRetention'] and row['missingFileStatus']==400
        rows.append(dict(caseId=row['caseId'],dimension=d,shape=list(shape),fileSha256=before,
            producer=producer,queriedPoint=point,index=index,block=block,localIndices=[int(v) for v in local],
            logicalKey=key,level=level,valueBits=bits(value),cellMeasureBits=measure,
            rawValueCoordinatesBoundsMeasure='FP64 bit-identical',independentMatchCount=1,
            inspectorRawValueText='renderer-asserted',missingFileRetention='PASS',fileUnchanged=True))
    assert len(rows)==2 and sorted(row['dimension'] for row in rows)==[1,2]
    summary=dict(status='PASS',scope='Real ProjectPlotfileAudit + LocalHostProvider + production HTTP Host and isolated reader + native Electron wheel/pan/selection + actual Inspector; independent stored HDF5 point oracle, not human/scientific UAT',
        records=rows,httpResponseStatusCounts={str(s):sum(x['status']==s for x in trace['trace']) for s in [200,400]},
        limits=['Two existing t=0 files; DENS only','No desktop launcher or full App shell tested',
        'No cancellation/race or all pixels/fields tested in this node','Original unknown run_id remains unknown',
        'No new simulation, independent physics oracle, benchmark or Windows work'])
    with args.output.open('x') as stream:json.dump(summary,stream,indent=2);stream.write('\n')
    print(json.dumps(summary,indent=2))
if __name__=='__main__':main()
