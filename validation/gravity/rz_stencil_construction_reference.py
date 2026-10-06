#!/usr/bin/env python3
"""Exact root-coordinate rational reconstruction of the actual final RZ stencil.
Records discrepancies, never fits or relaxes a scientific threshold.
Raw face/array data remain in a fresh local root; scalar findings are publishable.
"""
import argparse,json,hashlib,subprocess
from pathlib import Path
from fractions import Fraction as F
from functools import lru_cache

def solve(matrix,rhs):
    a=[list(row)+[v] for row,v in zip(matrix,rhs)];n=len(a)
    for col in range(n):
        pivot=next((j for j in range(col,n) if a[j][col]),None)
        if pivot is None:raise ArithmeticError('exact Gram is singular')
        a[col],a[pivot]=a[pivot],a[col]
        v=a[col][col];a[col]=[x/v for x in a[col]]
        for j in range(n):
            if j!=col:
                v=a[j][col]
                a[j]=[x-v*y for x,y in zip(a[j],a[col])]
    return tuple(row[-1] for row in a)

@lru_cache(maxsize=None)
def fit(basis,initial,boundary,axis):
    n=6;g=[[F(0) for _ in range(n)] for _ in range(n)]
    rhs=[F(int(j==1+axis)) for j in range(n)]
    if boundary:g[0][0]=1;rhs[0]-=boundary
    weights=[]
    for p,seed in zip(basis,initial):
        weight=1/(1+p[1]**2+p[2]**2)**2;weights.append(weight)
        for j in range(n):
            rhs[j]-=seed*p[j]
            for k in range(n):g[j][k]+=weight*p[j]*p[k]
    lam=solve(g,rhs)
    values=tuple(seed+w*sum((p[j]*lam[j] for j in range(n)),F(0))
                 for p,w,seed in zip(basis,weights,initial))
    b=boundary+lam[0] if boundary else F(0)
    # Exact ideal fit must meet all polynomial constraints. This does not
    # assert that actual floating coefficients satisfy a new numerical gate.
    for j in range(n):
        assert sum((v*p[j] for v,p in zip(values,basis)),F(0))+(b if j==0 else 0)==int(j==1+axis)
    return values,b

def reconstruct(c,f,neighbors):
    cells=c['cells'];spacing=list(map(F,c['spacing']));origin=list(map(F,c['origin']))
    def width(i,a):return spacing[a]/2**cells[i]['level']
    def center(i):return tuple(origin[a]+(F(cells[i]['index'][a])+F(1,2))*width(i,a) for a in range(2))
    left,right,axis=f['left'],f['right'],f['axis'];anchor=left if left>=0 else right
    boundary=f['boundary_side']>=0
    if boundary:
        center_f=list(center(anchor));side=f['boundary_side']%2
        center_f[axis]+=(-F(1,2) if not side else F(1,2))*width(anchor,axis)
        seed={anchor:(-2 if side else 2)/width(anchor,axis)}
        bc=-seed[anchor];scale=width(anchor,axis)
    else:
        fine=left if cells[left]['level']>=cells[right]['level'] else right
        center_f=list(center(fine));center_f[axis]=origin[axis]+(cells[left]['index'][axis]+1)*width(left,axis)
        inv=1/(width(left,axis)/2+width(right,axis)/2)
        seed={left:-inv,right:inv};bc=F(0);scale=max(width(left,axis),width(right,axis))
    source=list(seed)
    if f['construction']==1:
        for _ in range(2):
            source=sorted(set(source).union(*(neighbors[i] for i in source)))
        assert source==f['samples']
        basis=[]
        for i in source:
            x,y=((center(i)[a]-center_f[a])/scale for a in range(2))
            basis.append((F(1),x,y,x*x,x*y,y*y))
        values,bc_s=fit(tuple(basis),tuple(seed.get(i,F(0))*scale for i in source),bc*scale,axis)
        values=[v/scale for v in values];bc=bc_s/scale
        # Canonical final anchor reset is mathematically the same constraint.
        j=source.index(anchor);values[j]=-sum((v for k,v in enumerate(values) if k!=j),F(0))-bc
    else:
        assert source==f['samples'];values=[seed[i] for i in source]
    return values,bc,center_f

def main():
    p=argparse.ArgumentParser();p.add_argument('--probe',required=True,type=Path)
    p.add_argument('--output-root',required=True,type=Path);a=p.parse_args()
    out=a.output_root.resolve()
    if out.exists():p.error('output-root must be new')
    out.mkdir(parents=True)
    q=subprocess.run([str(a.probe.resolve()),'rz-stencil-probe'],capture_output=True,text=True)
    (out/'probe.json').write_text(q.stdout);(out/'stderr.log').write_text(q.stderr)
    if q.returncode:raise SystemExit(q.returncode)
    rows=[];all_faces=all_terms=0
    for c in json.loads(q.stdout)['cases']:
        neighbors=[set() for _ in c['cells']]
        for f in c['faces']:
            if f['left']>=0 and f['right']>=0:
                neighbors[f['left']].add(f['right']);neighbors[f['right']].add(f['left'])
        counts=[0,0,0];max_error=F(0);max_bc=F(0);max_center=F(0);max_constant=F(0)
        for f in c['faces']:
            values,bc,cf=reconstruct(c,f,neighbors);counts[f['construction']]+=1
            assert len(values)==len(f['coefficients'])
            max_error=max(max_error,max(abs(F(v)-exact) for v,exact in zip(f['coefficients'],values)))
            max_bc=max(max_bc,abs(F(f['boundary_coefficient'])-bc))
            max_center=max(max_center,max(abs(F(f['center'][a])-cf[a]) for a in range(2)))
            max_constant=max(max_constant,abs(sum(map(F,f['coefficients']),F(0))+F(f['boundary_coefficient'])))
            assert sum(values,F(0))+bc==0
            all_terms+=len(values)+1
        all_faces+=len(c['faces'])
        rows.append({'mixed':bool(c['mixed']),'radialOrigin':c['origin'][0],'spacing':c['spacing'][:2],
            'faces':len(c['faces']),'twoPoint':counts[0],'polynomialFit':counts[1],'ellipticRecovery':counts[2],
            'maxAbsoluteCoefficientDiscrepancy':float(max_error),'maxAbsoluteBoundaryCoefficientDiscrepancy':float(max_bc),
            'maxRootCoordinateDiscrepancy':float(max_center),'maxStoredConstantConstraintDefect':float(max_constant)})
    result={'status':'EXACT_REFERENCE_RECONSTRUCTION_PASS','cases':len(rows),'faces':all_faces,
        'coefficientTerms':all_terms,'rows':rows,'probeSha256':hashlib.sha256(a.probe.read_bytes()).hexdigest(),
        'scope':'Actual final stencil path; exact root-coordinate Gram and rational solve; scalar discrepancies only',
        'limitations':['Not a production outward coefficient certificate or error bound for arbitrary mesh',
            'No observed error used to select a tolerance','Recovery checked as two-point, never claimed quadratic',
            'No physical RHS/Phi/force acceptance, simulation, CUDA or long-run claim']}
    (out/'summary.json').write_text(json.dumps(result,indent=2)+'\n')
    print('RZ_STENCIL_EXACT_REFERENCE_PASS',len(rows),all_faces,all_terms)
if __name__=='__main__':main()
