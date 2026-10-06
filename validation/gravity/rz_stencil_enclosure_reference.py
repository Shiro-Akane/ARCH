#!/usr/bin/env python3
"""Independent exact coefficients and Gram inverse vs actual outward ledger.
Uses the independent root-coordinate Fraction reference, never production LU
or interval arithmetic. Full vectors are local; published output is scalar.
"""
import argparse,json,subprocess,hashlib
from pathlib import Path
from fractions import Fraction as F
from functools import lru_cache
from rz_stencil_construction_reference import reconstruct,solve

@lru_cache(maxsize=None)
def inverse_norm(matrix):
    columns=[solve(matrix,tuple(F(int(j==col)) for j in range(6))) for col in range(6)]
    return max(sum((abs(columns[col][row]) for col in range(6)),F(0)) for row in range(6))

def exact_gram(c,f,center_f):
    def width(i,a):return F(c['spacing'][a])/2**c['cells'][i]['level']
    def center(i,a):return F(c['origin'][a])+(F(c['cells'][i]['index'][a])+F(1,2))*width(i,a)
    boundary=f['boundary_side']>=0
    anchor=f['left'] if f['left']>=0 else f['right'];axis=f['axis']
    scale=width(anchor,axis) if boundary else max(width(f['left'],axis),width(f['right'],axis))
    g=[[F(int(boundary and j==0 and k==0)) for k in range(6)] for j in range(6)]
    for cell in f['samples']:
        x,y=((center(cell,a)-center_f[a])/scale for a in range(2))
        p=(F(1),x,y,x*x,x*y,y*y);weight=1/(1+x*x+y*y)**2
        for j in range(6):
            for k in range(6):g[j][k]+=weight*p[j]*p[k]
    return tuple(tuple(row) for row in g)

def main():
    p=argparse.ArgumentParser();p.add_argument('--probe',required=True,type=Path)
    p.add_argument('--output-root',required=True,type=Path);a=p.parse_args()
    out=a.output_root.resolve()
    if out.exists():p.error('output-root must be new')
    out.mkdir(parents=True)
    q=subprocess.run([str(a.probe.resolve()),'rz-stencil-probe'],capture_output=True,text=True)
    (out/'probe.json').write_text(q.stdout);(out/'stderr.log').write_text(q.stderr)
    if q.returncode:raise SystemExit(q.returncode)
    rows=[];terms=faces=0
    for c in json.loads(q.stdout)['cases']:
        neighbors=[set() for _ in c['cells']]
        for f in c['faces']:
            if f['left']>=0 and f['right']>=0:
                neighbors[f['left']].add(f['right']);neighbors[f['right']].add(f['left'])
        max_width=F(0);max_error=F(0);max_bound=F(0);max_q=0.;fit_count=0
        for f in c['faces']:
            exact,bc,cf=reconstruct(c,f,neighbors)
            for j,v in enumerate(exact):
                lo=F(f['coefficient_lower'][j]);hi=F(f['coefficient_upper'][j])
                assert lo<=v<=hi,(j,v,lo,hi)
                error=abs(F(f['coefficients'][j])-v)
                bound=F(f['coefficient_error_upper'][j]);assert error<=bound
                max_width=max(max_width,hi-lo);max_error=max(max_error,error);max_bound=max(max_bound,bound)
            assert F(f['boundary_lower'])<=bc<=F(f['boundary_upper'])
            assert abs(F(f['boundary_coefficient'])-bc)<=F(f['boundary_error_upper'])
            if f['construction']==1:
                assert 0<=f['inverse_residual_upper']<1
                inv=inverse_norm(exact_gram(c,f,cf))
                assert inv<=F(f['inverse_norm_upper'])
                fit_count+=1;max_q=max(max_q,f['inverse_residual_upper'])
            terms+=len(exact)+1;faces+=1
        rows.append({'mixed':bool(c['mixed']),'radialOrigin':c['origin'][0],'spacing':c['spacing'][:2],
            'faces':len(c['faces']),'hierarchyLevel':c.get('hierarchy_level',-1),'polynomialFits':fit_count,'maximumInverseResidualUpper':max_q,
            'maximumCoefficientWidth':float(max_width),'maximumMeasuredCoefficientError':float(max_error),
            'maximumCoefficientErrorUpper':float(max_bound)})
    result={'status':'PASS','cases':len(rows),'faces':faces,'coefficientTerms':terms,'rows':rows,
        'probeSha256':hashlib.sha256(a.probe.read_bytes()).hexdigest(),
        'scope':'Root-dyadic ideal final RZ stencil interval containment and exact Gram inverse norm',
        'limitations':['No face-area/source/observer construction or physical RHS/Phi/force claim',
            'Actual recovery branch not triggered by this fixture','No tolerance fitted from observed discrepancy',
            'Raw native vectors retained locally; no simulation/CUDA/long-run claim']}
    (out/'summary.json').write_text(json.dumps(result,indent=2)+'\n')
    print('RZ_STENCIL_ENCLOSURE_PASS',len(rows),faces,terms)
if __name__=='__main__':main()
