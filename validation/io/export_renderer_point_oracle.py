#!/usr/bin/env python3
'''Read-only h5py stored-value oracle. No physical oracle or publisher certification.'''
import argparse, hashlib, json, struct
from pathlib import Path
import h5py
def bits(v): return struct.pack('<d',float(v)).hex()
def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--plot',action='append',required=True,type=Path)
    ap.add_argument('--output',required=True,type=Path)
    args=ap.parse_args();rows=[]
    for path in args.plot:
        before=hashlib.sha256(path.read_bytes()).hexdigest()
        with h5py.File(path,'r') as f:
            d=int(f.attrs['dim']);assert d in (1,2) and f.attrs['geometry']=='cartesian'
            ds=f['Data/DENS'];assert ds.dtype.kind=='f' and ds.dtype.itemsize==8
            shape=ds.shape;assert len(shape)==d+1 and all(n>=3 for n in shape[1:])
            levels=f['Grid/level'][:];block=int(levels.argmax())
            local=[1]*d
            index=block*(shape[1] if d==1 else shape[1]*shape[2])+(1 if d==1 else shape[2]+1)
            value=float(ds[tuple([block]+local)])
            rows.append(dict(path=str(path.resolve()),sha256=before,dimension=d,block=block,
                row=1 if d==1 else 4,count=[3] if d==1 else [2,3],start=[0]*d,field='DENS',index=index,
                valueBits=bits(value),value=value,
                lower={a:float(f['NativeGrid/'+a+'_lower'][index]) for a in ['x1','x2','x3']},
                upper={a:float(f['NativeGrid/'+a+'_upper'][index]) for a in ['x1','x2','x3']},
                measureBits=bits(f['NativeGrid/cell_measure'][index]),level=int(levels[block]),
                logicalKey='/'.join(map(str,[int(levels[block])]+[int(f['NativeGrid/logical_'+a][block]) for a in ['x1','x2','x3']])),
                producer={k:str(f['SourceIdentity'].attrs[k]) for k in ['case_id','raw_config_sha256','binary_sha256','run_id']}))
        assert hashlib.sha256(path.read_bytes()).hexdigest()==before
    with args.output.open('x') as stream:json.dump(rows,stream,indent=2);stream.write('\n')
    print('Stored oracle exported without modifying HDF5')
if __name__=='__main__':main()
