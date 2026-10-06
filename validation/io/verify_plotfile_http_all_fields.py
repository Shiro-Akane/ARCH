#!/usr/bin/env python3
'''Independent full native HDF5 fields vs production HTTP trace; raw arrays stay local.'''
import argparse,hashlib,json,struct
from pathlib import Path
import h5py
import numpy as np
from verify_plotfile_fields import load_plot_json, verify_plot_identity

def bits(value): return struct.pack('<d',float(value))
def check_bits(actual,expected):
    assert len(actual)==len(expected)
    for a,e in zip(actual,expected):
        if isinstance(a,str):
            assert a in ('NaN','Infinity','-Infinity')
            assert (a=='NaN' and np.isnan(e)) or (a=='Infinity' and e==np.inf) or (a=='-Infinity' and e==-np.inf)
        else: assert bits(a)==bits(e)
def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--root',type=Path,required=True)
    ap.add_argument('--trace',type=Path,required=True)
    ap.add_argument('--output',type=Path,required=True)
    args=ap.parse_args();root=args.root.resolve();files={};handles={}
    try:
        for line in args.trace.open():
            row=load_plot_json(line);req=row['request'];path=(root/req['relativePath']).resolve()
            assert path.is_relative_to(root)
            if row['route']=='metadata':
                assert str(path) not in files
                f=h5py.File(path,'r');handles[str(path)]=f
                m=row['response']['metadata']
                sha=hashlib.sha256(path.read_bytes()).hexdigest()
                assert sha==m['file']['sha256']
                assert int(f.attrs['dim']) in (1,2) and f.attrs['geometry']=='cartesian'
                assert set(f['Data'])=={x['name'] for x in m['fields']}
                identity=m['candidateSourceIdentity']
                for api,stored in (('caseId','case_id'),('rawConfigSha256','raw_config_sha256'),('binarySha256','binary_sha256')):
                    assert identity[api]==str(f['SourceIdentity'].attrs[stored])
                run=str(f['SourceIdentity'].attrs['run_id'])
                assert identity['runId']==(None if run=='unknown' else run)
                verified_identity=verify_plot_identity(f,observed=m,expected_sha=sha)
                files[str(path)]={'fileSha256':sha,'seen':set(),'values':0,'metadata':m,'identity':verified_identity}
                continue
            assert row['route']=='slice'
            f=handles[str(path)];state=files[str(path)];m=state['metadata']
            result=row['response']['result'];p=result['payload'];n=p['nativeCells'];s=req['slice']
            field=s['field'];block=s['block'];assert (field,block) not in state['seen']
            state['seen'].add((field,block))
            ds=f['Data/'+field];assert ds.dtype.kind=='f' and ds.dtype.itemsize==8
            assert list(ds.shape)==[m['blocks']]+m['cellShape']
            assert s['start']==[0]*(len(ds.shape)-1) and s['count']==list(ds.shape[1:])
            expected=ds[block].reshape(-1);check_bits(p['values'],expected)
            per=len(expected);lo=block*per;hi=lo+per
            assert p['linearIndices']==list(range(lo,hi))
            for axis in ('x','y','z'):check_bits(p['coordinates'][axis],f['Grid/'+axis][lo:hi])
            for axis in ('x1','x2','x3'):
                for edge in ('lower','upper'):check_bits(n[edge][axis],f['NativeGrid/'+axis+'_'+edge][lo:hi])
            check_bits(n['cellMeasure'],f['NativeGrid/cell_measure'][lo:hi])
            level=int(f['Grid/level'][block])
            key='/'.join(map(str,[level]+[int(f['NativeGrid/logical_'+a][block]) for a in ('x1','x2','x3')]))
            assert n['logicalKey']==key and n['level']==level
            decl=next(x for x in m['fields'] if x['name']==field)
            attrs={k:str(v) for k,v in ds.attrs.items()}
            stored_unit=None if attrs['unit']=='unknown' else attrs['unit']
            assert p['unit']==decl['unit']==stored_unit
            for key in ('basis','centering','meaning'):
                assert decl['declaration'][key]==attrs[key]
            assert decl['declaration']['unit']==stored_unit
            assert decl['declaration']['unitReason']==attrs.get('unit_reason')
            assert result['candidateSourceIdentity']==m['candidateSourceIdentity']
            assert result['candidateNativeGrid']==m['candidateNativeGrid']
            assert result['file']['sha256']==state['fileSha256']==req['expectedFileSha256']
            state['values']+=per
        rows=[]
        for path,state in files.items():
            f=handles[path];m=state['metadata']
            expected={(field,b) for field in f['Data'] for b in range(m['blocks'])}
            assert state['seen']==expected and state['values']==m['cells']*len(f['Data'])
            assert hashlib.sha256(Path(path).read_bytes()).hexdigest()==state['fileSha256']
            rows.append({'caseId':str(f['SourceIdentity'].attrs['case_id']),'fileSha256':state['fileSha256'],
                'shape':list(f['Data/DENS'].shape),'fields':sorted(f['Data']),'fieldCount':len(f['Data']),
                'blocks':m['blocks'],'cells':m['cells'],'verifiedFieldValues':state['values'],
                'levels':sorted(set(map(int,f['Grid/level'][:]))),
                'producerBinarySha256':str(f['SourceIdentity'].attrs['binary_sha256']),
                'runId':str(f['SourceIdentity'].attrs['run_id']),
                'identity':state['identity'],
                'rawFieldsCoordinatesBoundsMeasure':'FP64 bit-identical for every active leaf cell',
                'fieldUnitBasisCenteringMeaning':'exact stored declaration match','fileUnchanged':True})
        assert len(rows)==2
        output={'status':'PASS','scope':'Full native values and declarations via production HTTP + client validation, independent h5py. Storage/readback consistency only.',
            'rows':rows,'limits':['Recorded local t=0 files; embedded producer identity, no selected-binary freshness inference',
                'No physics/EOS oracle or unit declaration scientific signoff','No renderer all-field UAT',
                'NaN/Infinity encoded as named JSON tokens; no nonfinite payload-bit certification',
                'First read scans/hash cost not bounded by returned pixel count','No simulation or Windows work']}
        with args.output.open('x') as stream:json.dump(output,stream,indent=2);stream.write('\n')
        print(json.dumps(output,indent=2))
    finally:
        for f in handles.values():f.close()
if __name__=='__main__':main()
