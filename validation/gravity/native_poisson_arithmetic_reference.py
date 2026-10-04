#!/usr/bin/env python3
"""Exact-rational stored native operator arithmetic reference.
Every parsed FP64 input is converted to an exact Fraction. No production
arithmetic helpers, stencils builders, norm, physics or interval code imported.
Raw native arrays/stencils are persisted locally, processed scalars delivered.
"""
import argparse,json,subprocess
from fractions import Fraction as F
from pathlib import Path

def exact(v):return F.from_float(float(v))
def main():
    ap=argparse.ArgumentParser();ap.add_argument('--probe',required=True,type=Path)
    ap.add_argument('--output',required=True,type=Path);a=ap.parse_args()
    q=subprocess.run([str(a.probe.resolve()),'arithmetic-ledger-probe'],capture_output=True,text=True,check=True)
    a.output.parent.mkdir(parents=True,exist_ok=True)
    a.output.with_suffix('.raw.json').write_text(q.stdout)
    data=json.loads(q.stdout);assert data['negativePass'];rows=[]
    for c in data['cases']:
        phi,source,boundary,rhs,residual,volumes,weights=(
            list(map(exact,c[key])) for key in ('phi','source','boundary','rhs','residual','volumes','weights'))
        b=list(source);ax=[F(0) for _ in source]
        for i,face in enumerate(c['faces']):
            left,right=face['left'],face['right'];anchor=left if left>=0 else right
            area,bc=exact(face['area']),exact(face['bc'])
            flux=area*bc*boundary[i]
            gradient=bc*(-phi[anchor])
            for sample,coef in zip(face['samples'],face['coefficients']):
                gradient+=exact(coef)*(phi[sample]-phi[anchor])
            if left>=0:
                b[left]+=flux/volumes[left];ax[left]-=area*gradient/volumes[left]
            if right>=0:
                b[right]-=flux/volumes[right];ax[right]+=area*gradient/volumes[right]
        e_rhs=[abs(x-y) for x,y in zip(rhs,b)]
        e_res=[abs(value-(x-y)) for value,x,y in zip(residual,ax,rhs)]
        ratios=[]
        for err,bounds,norm in ((e_rhs,c['rhsBounds'],c['rhsNormUpper']),
                                (e_res,c['residualBounds'],c['residualNormUpper'])):
            for e,limit in zip(err,bounds):
                assert e<=exact(limit),('cell bound violated',c['rz'],c['mixed'],c['lane'],str(e),limit)
                if limit:ratios.append(float(e/exact(limit)))
            norm_squared=sum(w*e*e for w,e in zip(weights,err))
            assert norm_squared<=exact(norm)**2,('stored-weight norm bound violated',c['lane'])
        if c['lane']==0:assert all(e==0 for e in e_rhs+e_res) and c['rhsNormUpper']==c['residualNormUpper']==0
        rows.append({'rz':bool(c['rz']),'mixed':bool(c['mixed']),'origin':c['origin'],'lane':c['lane'],
            'cells':len(source),'rhsActualErrorMax':float(max(e_rhs)),
            'residualActualErrorMax':float(max(e_res)),
            'rhsNormUpper':c['rhsNormUpper'],'residualNormUpper':c['residualNormUpper'],
            'maxBoundUtilization':max(ratios,default=0.),'exactRationalContainment':True})
    assert len(rows)==18
    a.output.write_text(json.dumps({'status':'PASS','cases':len(rows),'rows':rows,
        'reference':'exact Fraction arithmetic over FP64 native operator arrays',
        'scope':'stored source/coefficient/volume/weight evaluation only; excludes geometry/source construction and full physical residual acceptance'},indent=2)+'\n')
    print('NATIVE_POISSON_ARITHMETIC_FRACTION_PASS',len(rows))
if __name__=='__main__':main()
