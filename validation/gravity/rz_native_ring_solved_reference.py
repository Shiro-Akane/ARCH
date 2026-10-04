#!/usr/bin/env python3
"""Independent native RHS/residual box and original tolerance for actual solves.
Exact Fraction root geometry/Gram, no production solve or interval helpers.
This is discrete boundary-box acceptance, not continuous Phi/force science.
"""
import argparse,hashlib,json
from fractions import Fraction as F
from pathlib import Path
from rz_stencil_construction_reference import reconstruct
def main():
    p=argparse.ArgumentParser();p.add_argument('--probe-record',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    if a.output.exists():p.error('output must be new')
    rows=[]
    try:record=json.loads(a.probe_record.read_text())
    except json.JSONDecodeError:
        raise SystemExit('No complete successful probe record; failed/partial output cannot certify a solved field')
    for c in record['cases']:
        cells=c['cells'];neighbors=[set() for _ in cells]
        for f in c['faces']:
            if f['left']>=0 and f['right']>=0:
                neighbors[f['left']].add(f['right']);neighbors[f['right']].add(f['left'])
        def width(i,axis):return F(c['spacing'][axis])/2**cells[i]['level']
        def radial(i):
            lo=F(c['origin'][0])+cells[i]['index'][0]*width(i,0)
            return (lo+width(i,0))**2-lo**2
        volumes=[radial(i)*width(i,1) for i in range(len(cells))]
        weights=[v/sum(volumes) for v in volumes];phi=list(map(F,c['potential']))
        applied=[F(0) for _ in cells]
        lower=list(map(F,c['source_lower']));upper=list(map(F,c['source_upper']))
        for f in c['faces']:
            coef,bc,center=reconstruct(c,f,neighbors);boundary=f['boundary_side']>=0
            anchor=f['left'] if f['left']>=0 else f['right']
            fine=anchor if boundary else (f['left'] if cells[f['left']]['level']>=cells[f['right']]['level'] else f['right'])
            area=2*center[0]*width(fine,1) if f['axis']==0 else radial(fine)
            gradient=sum((co*(phi[i]-phi[anchor]) for co,i in zip(coef,f['samples'])),F(0))-bc*phi[anchor]
            for side,i in enumerate((f['left'],f['right'])):
                if i<0:continue
                sign=-1 if side==0 else 1;ratio=area/volumes[i]
                applied[i]+=sign*ratio*gradient
                if boundary:
                    mapping=-sign*ratio*bc;k=f['index']
                    ends=[mapping*F(c['face_lower'][k]),mapping*F(c['face_upper'][k])]
                    lower[i]+=min(ends);upper[i]+=max(ends)
        rhs_error=[];eval_error=[];full=[];minimum_rhs=[]
        for i in range(len(cells)):
            rhs_error.append(max(abs(F(c['rhs'][i])-lower[i]),abs(F(c['rhs'][i])-upper[i])))
            eval_error.append(abs(F(c['residual'][i])-(applied[i]-F(c['rhs'][i]))))
            assert rhs_error[-1]<=F(c['rhs_error_cells'][i])
            assert eval_error[-1]<=F(c['evaluation_cells'][i])
            full.append(max(abs(applied[i]-lower[i]),abs(applied[i]-upper[i])))
            minimum_rhs.append(0 if lower[i]<=0<=upper[i] else min(abs(lower[i]),abs(upper[i])))
        norm2=lambda xs:sum((w*x*x for w,x in zip(weights,xs)),F(0))
        assert norm2(rhs_error)<=F(c['rhs_error_upper'])**2
        assert norm2(full)<=F(c['total_residual_upper'])**2
        assert F(c['total_residual_upper'])<=F(c['tolerance_safe'])
        # Direct independent original-request guarantee for every RHS in box.
        assert norm2(full)<=F(1e-10)**2*norm2(minimum_rhs)
        assert F(c['rhs_norm_lower'])**2<=norm2(list(map(F,c['rhs'])))
        rows.append({'radialOrigin':c['radial_origin'],'cells':len(cells),'faces':len(c['faces']),
            'maximumExactRhsBoxDistance':float(max(rhs_error)),
            'maximumExactEvaluationError':float(max(eval_error)),
            'maximumExactOriginalResidualBoxDistance':float(max(full)),
            'totalResidualUpper':c['total_residual_upper'],'toleranceSafe':c['tolerance_safe'],
            'rhsErrorUpper':c['rhs_error_upper'],'work':c['work'],'rangeEvaluations':c['range_evaluations']})
    result={'status':'PASS','cases':len(rows),'cells':sum(r['cells'] for r in rows),'rows':rows,
        'probeRecordSha256':hashlib.sha256(a.probe_record.read_bytes()).hexdigest(),
        'scope':'Actual solved nonzero-source native discrete original request rtol=1e-10 atol=0, given certified finite-ring boundary intervals',
        'limitations':['Exact-coordinate subset, two uniform 4x4 roots only','Not independent continuous Phi/force or full RZ release',
            'Boundary integral enclosure is checked by existing ring proofs; this script checks its native consumption',
            'No timestep/simulation, CUDA or long-run claim; raw arrays remain local']}
    a.output.parent.mkdir(parents=True,exist_ok=True)
    a.output.write_text(json.dumps(result,indent=2)+'\n')
    print('NATIVE_RING_SOLVED_FRACTION_PASS',len(rows),result['cells'])
if __name__=='__main__':main()
