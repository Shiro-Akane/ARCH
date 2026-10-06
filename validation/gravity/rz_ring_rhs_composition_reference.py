#!/usr/bin/env python3
"""Exact Fraction propagation of source/face intervals through stored B.
Raw native arrays are local evidence; output contains only processed scalar metrics.
This proves the composed stored-operator enclosure, not physical construction.
"""
import argparse,json,subprocess,hashlib
from pathlib import Path
from fractions import Fraction as F
from decimal import Decimal as D,localcontext
from rz_ring_axis_reference import PI

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--probe',type=Path,required=True);ap.add_argument('--output-root',type=Path,required=True)
    a=ap.parse_args();out=a.output_root.resolve()
    if out.exists():ap.error('output-root must be new')
    out.mkdir(parents=True)
    q=subprocess.run([str(a.probe.resolve()),'ring-rhs-probe'],text=True,capture_output=True)
    (out/'probe.json').write_text(q.stdout);(out/'stderr.log').write_text(q.stderr)
    if q.returncode:raise SystemExit(q.returncode)
    rows=[];cells=0
    for case in json.loads(q.stdout)['cases']:
        lower=list(map(F,case['source_lower']));upper=list(map(F,case['source_upper']))
        for rho,lo,hi in zip(case['density'],case['source_lower'],case['source_upper']):
            for precision in (100,140):
                with localcontext() as ctx:
                    ctx.prec=precision
                    source=-4*PI*D.from_float(6.67430e-8)*D.from_float(rho)
                    assert D.from_float(lo)<=source<=D.from_float(hi)
        for face in case['faces']:
            k=face['index']
            for cell,sign in ((face['left'],1),(face['right'],-1)):
                if cell<0:continue
                coefficient=sign*F(face['area'])*F(face['boundary_coefficient'])/F(case['volumes'][cell])
                products=[coefficient*F(case['face_lower'][k]),coefficient*F(case['face_upper'][k])]
                lower[cell]+=min(products);upper[cell]+=max(products)
        distances=[]
        for i,(lo,hi) in enumerate(zip(lower,upper)):
            actual=F(case['rhs'][i]);distance=max(abs(actual-lo),abs(actual-hi))
            assert distance<=F(case['combined_cells'][i]),(i,distance,case['combined_cells'][i])
            distances.append(distance)
        exact_error_norm2=sum((F(w)*err**2 for w,err in zip(case['weights'],distances)),F(0))
        exact_bound_norm2=sum((F(w)*F(e)**2 for w,e in zip(case['weights'],case['combined_cells'])),F(0))
        assert exact_error_norm2<=F(case['combined_norm_upper'])**2
        assert exact_bound_norm2<=F(case['combined_norm_upper'])**2
        if case['zero']:
            assert all(d==0 for d in distances) and case['total_residual_upper']==0 and case['tolerance_safe']==0
        else:
            assert case['total_residual_upper']>case['tolerance_safe']
        cells+=len(distances)
        rows.append({'mixed':bool(case['mixed']),'radialOrigin':case['origin'],'zero':bool(case['zero']),
            'cells':len(distances),'maximumExactIntervalDistance':float(max(distances)),
            'sourceNormUpper':case['source_norm_upper'],'boundaryNormUpper':case['boundary_norm_upper'],
            'assemblyNormUpper':case['assembly_norm_upper'],'residualEvaluationNormUpper':case['residual_norm_upper'],
            'combinedRhsNormUpper':case['combined_norm_upper'],'totalResidualUpper':case['total_residual_upper'],
            'toleranceSafe':case['tolerance_safe']})
    result={'status':'PASS','cases':len(rows),'cells':cells,'rows':rows,
        'scope':'Identity-bound physical source and ring interval composition through exact stored B/native weights; physical geometry/coefficient/weight construction unverified',
        'probeSha256':hashlib.sha256(a.probe.read_bytes()).hexdigest(),
        'limitations':['Conditional zero accepted, positive un-solved arrays rejected at unchanged rtol=1e-10 atol=0',
            'No physical acceptance, field publication, new simulation or CUDA claim']}
    (out/'summary.json').write_text(json.dumps(result,indent=2)+'\n')
    print('RZ_RING_RHS_FRACTION_PASS',len(rows),cells)
if __name__=='__main__':main()
