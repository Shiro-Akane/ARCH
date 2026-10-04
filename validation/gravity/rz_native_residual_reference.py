#!/usr/bin/env python3
"""Exact native A, homogeneous boundary term, actual apply/residual and RHS box.
Root-dyadic exact geometry/fitted reference is independent of production LU.
Raw arrays stay local; output publishes scalar errors/identities only.
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
    rows=[];nc=nf=0
    for c in json.loads(q.stdout)['cases']:
        neighbors=[set() for _ in c['cells']]
        for face in c['faces']:
            if face['left']>=0 and face['right']>=0:
                neighbors[face['left']].add(face['right']);neighbors[face['right']].add(face['left'])
        def width(i,a):return F(c['spacing'][a])/2**c['cells'][i]['level']
        def radial(i):
            lo=F(c['origin'][0])+c['cells'][i]['index'][0]*width(i,0)
            return (lo+width(i,0))**2-lo**2
        volumes=[radial(i)*width(i,1) for i in range(len(c['cells']))]
        total=sum(volumes,F(0));weights=[v/total for v in volumes]
        ideal=[F(0) for _ in volumes];stored=ideal.copy();rhslo=ideal.copy();rhshi=ideal.copy()
        phi=list(map(F,c['potential']))
        for index,f in enumerate(c['faces']):
            coefficients,bc,center=reconstruct(c,f,neighbors);boundary=f['boundary_side']>=0
            anchor=f['left'] if f['left']>=0 else f['right']
            fine=anchor if boundary else (f['left'] if c['cells'][f['left']]['level']>=c['cells'][f['right']]['level'] else f['right'])
            area=2*center[0]*width(fine,1) if f['axis']==0 else radial(fine)
            gradient=sum((coef*(phi[cell]-phi[anchor]) for coef,cell in zip(coefficients,f['samples'])),F(0))-bc*phi[anchor]
            stored_gradient=sum((F(coef)*(phi[cell]-phi[anchor]) for coef,cell in zip(f['coefficients'],f['samples'])),F(0))-F(f['boundary_coefficient'])*phi[anchor]
            for side,i in enumerate((f['left'],f['right'])):
                if i<0:continue
                orientation=-1 if side==0 else 1
                ratio=area/volumes[i];stored_ratio=F(f['area'])/F(c['stored_volumes'][i])
                ideal[i]+=orientation*ratio*gradient;stored[i]+=orientation*stored_ratio*stored_gradient
                if boundary:
                    mapping=-orientation*ratio*bc
                    value=F(c['face_values'][index]);error=F(c['potential_errors'][index])
                    ends=[mapping*(value-error),mapping*(value+error)]
                    rhslo[i]+=min(ends);rhshi[i]+=max(ends)
        construction=[abs(x-y) for x,y in zip(ideal,stored)]
        evaluation=[abs(F(actual)-(x-F(rhs))) for actual,x,rhs in zip(c['computed_residual'],ideal,c['rhs'])]
        arithmetic=[abs(F(actual)-(x-F(rhs))) for actual,x,rhs in zip(c['computed_residual'],stored,c['rhs'])]
        full=[max(abs(F(actual)-(x-l)),abs(F(actual)-(x-h)))
              for actual,x,l,h in zip(c['computed_residual'],ideal,rhslo,rhshi)]
        for i in range(len(volumes)):
            assert construction[i]<=F(c['operator_construction_cells'][i])
            assert arithmetic[i]<=F(c['residual_arithmetic_cells'][i])
            assert evaluation[i]<=F(c['residual_evaluation_cells'][i])
            assert full[i]<=F(c['full_residual_error_cells'][i])
        for errors,bounds,norm in ((construction,c['operator_construction_cells'],c['operator_construction_native_norm_upper']),
                (evaluation,c['residual_evaluation_cells'],c['residual_evaluation_native_norm_upper']),
                (full,c['full_residual_error_cells'],c['full_residual_error_native_norm_upper'])):
            assert sum((w*e**2 for w,e in zip(weights,errors)),F(0))<=F(norm)**2
            assert sum((w*F(e)**2 for w,e in zip(weights,bounds)),F(0))<=F(norm)**2
        nc+=len(volumes);nf+=len(c['faces'])
        rows.append({'mixed':bool(c['mixed']),'hierarchyLevel':c.get('hierarchy_level',-1),
            'radialOrigin':c['origin'][0],'spacing':c['spacing'][:2],'cells':len(volumes),'faces':len(c['faces']),
            'maximumExactOperatorConstructionError':float(max(construction)),
            'maximumExactResidualArithmeticError':float(max(arithmetic)),
            'maximumExactNativeEvaluationError':float(max(evaluation)),
            'operatorConstructionNativeNormUpper':c['operator_construction_native_norm_upper'],
            'residualEvaluationNativeNormUpper':c['residual_evaluation_native_norm_upper'],
            'fullManufacturedResidualBoxNativeNormUpper':c['full_residual_error_native_norm_upper']})
    result={'status':'PASS','cases':len(rows),'cells':nc,'faces':nf,'rows':rows,
        'probeSha256':hashlib.sha256(a.probe.read_bytes()).hexdigest(),
        'scope':'Canonical homogeneous native A construction, actual scalar apply/subtraction arithmetic, and complete manufactured ideal-boundary residual interval box',
        'limitations':['Not a solved physical Poisson system or continuous Phi/force certificate',
            'Real ideal source/observer producer and source/AMR identity binding remain pending',
            'Natural recovery and all RZ consumer science remain pending','No threshold change, simulation/CUDA/long-run claim; raw arrays retained locally']}
    (out/'summary.json').write_text(json.dumps(result,indent=2)+'\n')
    print('RZ_NATIVE_RESIDUAL_FRACTION_PASS',len(rows),nc,nf)
if __name__=='__main__':main()
