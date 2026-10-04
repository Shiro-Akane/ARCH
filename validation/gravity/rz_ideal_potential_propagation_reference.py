#!/usr/bin/env python3
"""Exact ideal-B potential interval propagation and complete boundary error box.
Manufactured face uncertainty is not a real ring potential certificate.
No production solver/interval helpers imported; raw arrays stay local.
"""
import argparse,hashlib,json,subprocess
from pathlib import Path
from fractions import Fraction as F
from rz_stencil_construction_reference import reconstruct

def main():
    p=argparse.ArgumentParser();p.add_argument('--probe',required=True,type=Path)
    p.add_argument('--output-root',required=True,type=Path);a=p.parse_args()
    out=a.output_root.resolve()
    if out.exists():p.error('output-root must be new')
    out.mkdir(parents=True)
    q=subprocess.run([str(a.probe.resolve()),'rz-stencil-probe'],capture_output=True,text=True)
    (out/'probe.json').write_text(q.stdout);(out/'stderr.log').write_text(q.stderr)
    if q.returncode:raise SystemExit(q.returncode)
    rows=[];cells=faces=boundary=0
    for c in json.loads(q.stdout)['cases']:
        neighbors=[set() for _ in c['cells']]
        for f in c['faces']:
            if f['left']>=0 and f['right']>=0:
                neighbors[f['left']].add(f['right']);neighbors[f['right']].add(f['left'])
        def width(i,a):return F(c['spacing'][a])/2**c['cells'][i]['level']
        def radial(i):
            lo=F(c['origin'][0])+c['cells'][i]['index'][0]*width(i,0)
            return (lo+width(i,0))**2-lo**2
        volume=[radial(i)*width(i,1) for i in range(len(c['cells']))]
        total=sum(volume,F(0));weights=[v/total for v in volume]
        lo=[F(0) for _ in volume];hi=lo.copy();prop=lo.copy();count=0;max_q=0.;max_width=F(0)
        for index,f in enumerate(c['faces']):
            if f['construction']==1:max_q=max(max_q,f['inverse_residual_upper'])
            max_width=max(max_width,max(F(h)-F(l) for l,h in zip(f['coefficient_lower'],f['coefficient_upper'])))
            if f['boundary_side']<0:continue
            count+=1
            _,bc,center=reconstruct(c,f,neighbors);anchor=f['left'] if f['left']>=0 else f['right']
            area=2*center[0]*width(anchor,1) if f['axis']==0 else radial(anchor)
            value=F(c['face_values'][index]);error=F(c['potential_errors'][index])
            for side,i in enumerate((f['left'],f['right'])):
                if i<0:continue
                coefficient=(1 if side==0 else -1)*area/volume[i]*bc
                prop[i]+=abs(coefficient)*error
                ends=[coefficient*(value-error),coefficient*(value+error)]
                lo[i]+=min(ends);hi[i]+=max(ends)
        worst=[max(abs(F(actual)-l),abs(F(actual)-h)) for actual,l,h in zip(c['rhs'],lo,hi)]
        for i in range(len(volume)):
            assert prop[i]<=F(c['propagated_cells'][i])
            assert worst[i]<=F(c['total_boundary_cells'][i])
        for actual,bound,norm in ((prop,c['propagated_cells'],c['propagated_native_norm_upper']),
                                  (worst,c['total_boundary_cells'],c['total_boundary_native_norm_upper'])):
            assert sum((w*e**2 for w,e in zip(weights,actual)),F(0))<=F(norm)**2
            assert sum((w*F(e)**2 for w,e in zip(weights,bound)),F(0))<=F(norm)**2
        cells+=len(volume);faces+=len(c['faces']);boundary+=count
        rows.append({'mixed':bool(c['mixed']),'hierarchyLevel':c.get('hierarchy_level',-1),
            'radialOrigin':c['origin'][0],'spacing':c['spacing'][:2],'cells':len(volume),'faces':len(c['faces']),
            'boundaryFaces':count,'maximumInverseResidualUpper':max_q,
            'maximumCoefficientWidth':float(max_width),'potentialPropagationNativeNormUpper':c['propagated_native_norm_upper'],
            'fullManufacturedBoundaryBoxNativeNormUpper':c['total_boundary_native_norm_upper']})
    result={'status':'PASS','cases':len(rows),'cells':cells,'faces':faces,'boundaryFaces':boundary,'rows':rows,
        'probeSha256':hashlib.sha256(a.probe.read_bytes()).hexdigest(),
        'scope':'Ideal B error propagation and complete construction+potential+assembly box for explicitly root-scoped manufactured inputs',
        'limitations':['No existing stored-coordinate ring error automatically promoted',
            'Actual ideal source/observer potential producer and interior A/residual/Phi/force remain pending',
            'Actual recovery fixture remains pending; no threshold change, simulation, CUDA or long-run claim',
            'Raw vectors retained locally']}
    (out/'summary.json').write_text(json.dumps(result,indent=2)+'\n')
    print('RZ_IDEAL_POTENTIAL_PROPAGATION_PASS',len(rows),cells,faces,boundary)
if __name__=='__main__':main()
