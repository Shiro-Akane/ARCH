#!/usr/bin/env python3
"""Independent ideal-root-coordinate full-ring measures and RMS.
Exact Fraction cancels pi for normalized weights; high precision volume
diagnostics use the frozen pi reference. Raw arrays stay in a fresh local root.
No production interval helpers are imported.
"""
import argparse,hashlib,json,subprocess
from pathlib import Path
from fractions import Fraction as F
from decimal import Decimal as D,localcontext
from rz_ring_axis_reference import PI

def main():
    p=argparse.ArgumentParser()
    p.add_argument('--probe',required=True,type=Path)
    p.add_argument('--output-root',required=True,type=Path)
    a=p.parse_args();out=a.output_root.resolve()
    if out.exists():p.error('output-root must be new')
    out.mkdir(parents=True)
    proc=subprocess.run([str(a.probe.resolve()),'rz-measure-probe'],capture_output=True,text=True)
    (out/'probe.json').write_text(proc.stdout);(out/'stderr.log').write_text(proc.stderr)
    if proc.returncode:raise SystemExit(proc.returncode)
    rows=[];count=0
    for c in json.loads(proc.stdout)['cases']:
        measures=[]
        for cell in c['cells']:
            dr=F(c['spacing'][0])/2**cell['level']
            dz=F(c['spacing'][1])/2**cell['level']
            rl=F(c['origin'])+cell['index'][0]*dr
            measures.append((rl+dr)**2*dz-rl**2*dz)
        total=sum(measures,F(0));weights=[v/total for v in measures]
        for i,w in enumerate(weights):
            assert F(c['weight_lower'][i])<=w<=F(c['weight_upper'][i])
            assert abs(F(c['stored_weights'][i])-w)<=F(c['weight_error'][i])
        norm2=sum((w*F(x)**2 for w,x in zip(weights,c['values'])),F(0))
        assert F(c['norm_lower'])**2<=norm2<=F(c['norm_upper'])**2
        maximum_volume_error=0.
        for precision in (100,140):
            with localcontext() as ctx:
                ctx.prec=precision
                dv=[PI*D(v.numerator)/D(v.denominator) for v in measures]
                tv=PI*D(total.numerator)/D(total.denominator)
                assert D.from_float(c['total_lower'])<=tv<=D.from_float(c['total_upper'])
                for i,v in enumerate(dv):
                    assert D.from_float(c['volume_lower'][i])<=v<=D.from_float(c['volume_upper'][i])
                    error=abs(D.from_float(c['stored_volumes'][i])-v)
                    assert error<=D.from_float(c['volume_error'][i])
                    maximum_volume_error=max(maximum_volume_error,float(error))
        count+=len(measures)
        rows.append({'mixed':bool(c['mixed']),'radialOrigin':c['origin'],'spacing':c['spacing'][:2],
            'cells':len(measures),'maximumVolumeError':maximum_volume_error,
            'maximumVolumeErrorUpper':max(c['volume_error']),
            'maximumWeightErrorUpper':max(c['weight_error']),
            'physicalNormLower':c['norm_lower'],'physicalNormUpper':c['norm_upper']})
    result={'status':'PASS','cases':len(rows),'cells':count,'rows':rows,
        'probeSha256':hashlib.sha256(a.probe.read_bytes()).hexdigest(),
        'scope':'Ideal full-ring volumes and normalized physical RMS from exact dyadic root coordinates',
        'limitations':['No face-coefficient/fit certificate, no physical RHS acceptance or force gate',
            'No simulation, CUDA or long-run claim','Raw vectors retained only locally']}
    (out/'summary.json').write_text(json.dumps(result,indent=2)+'\n')
    print('RZ_NATIVE_MEASURE_PASS',len(rows),count)
if __name__=='__main__':main()
