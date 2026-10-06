#!/usr/bin/env python3
"""Independent exact-polynomial full-ring moment reference and Decimal Newton diagnostics.
No production moment, translation, kernel or tail implementation is imported.
Diagnostic quadrature differences are not reliable integral-error bounds.
Raw probe geometry/node arrays stay in the caller's local raw directory.
"""
import argparse,json,subprocess,hashlib,math
from decimal import Decimal as D,localcontext
from pathlib import Path
from rz_ring_far_leaf_reference import reference
from rz_ring_axis_reference import PI

def moments(leaves,center,precision):
    with localcontext() as ctx:
        ctx.prec=precision
        cz=D.from_float(center[2]);out=[D(0) for _ in range(10)]
        support2=D(0)
        for leaf in leaves:
            rl,rh,zl,zh,rho=map(D.from_float,leaf['ring'])
            # Direct polynomial integrals about the node's axis center;
            # no child moment translation is used by this reference.
            radial2=(rh**2-rl**2)/2
            mass=2*PI*rho*radial2*(zh-zl)
            firstz=2*PI*rho*radial2*((zh-cz)**2-(zl-cz)**2)/2
            transverse=PI*rho*(rh**4-rl**4)*(zh-zl)/4
            secondz=2*PI*rho*radial2*((zh-cz)**3-(zl-cz)**3)/3
            out[0]+=mass;out[3]+=firstz;out[4]+=transverse;out[7]+=transverse;out[9]+=secondz
            support2=max(support2,rh**2+max(abs(zl-cz),abs(zh-cz))**2)
        return out,support2

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--probe',type=Path,required=True)
    ap.add_argument('--output-root',type=Path,required=True)
    a=ap.parse_args();out=a.output_root.resolve()
    if out.exists():ap.error('output-root must be new')
    out.mkdir(parents=True)
    result=subprocess.run([str(a.probe.resolve()),'ring-parent-probe'],text=True,capture_output=True)
    (out/'probe.stdout.json').write_text(result.stdout)
    (out/'probe.stderr.log').write_text(result.stderr)
    if result.returncode:raise SystemExit(result.returncode)
    cases=json.loads(result.stdout)['cases'];rows=[]
    total_nodes=total_checks=0;newton_checks=0
    for case in cases:
        nodes=case['nodes'];leaf_nodes=[node for node in nodes if node['cell']>=0]
        for i,node in enumerate(nodes):
            leaves=[n for n in nodes[i:node['end']] if n['cell']>=0]
            assert len(leaves)==node['leaves']
            refs=[moments(leaves,node['center'],prec) for prec in (80,120)]
            for values,support2 in refs:
                with localcontext() as ctx:
                    ctx.prec=120
                    assert D.from_float(node['support_upper'])**2>=support2
                for value,bound in zip(values,node['moments']):
                    assert D.from_float(bound[0])<=value<=D.from_float(bound[1]),(i,value,bound)
                    total_checks+=1
            total_nodes+=1
        with localcontext() as ctx:
            ctx.prec=120
            root=nodes[0]
            values,support2=moments(leaf_nodes,root['center'],120)
            assert values[3]!=0  # Deliberately asymmetric full source.
            row={'mixed':bool(case['mixed']),'radialOrigin':case['origin'],'leafCount':len(leaf_nodes),
                'nodeCount':len(nodes),'independentMass':str(values[0]),
                'independentAxialDipole':str(values[3]),
                'supportUpper':root['support_upper'],'independentSupportSquared':str(support2),
                'farChecks':[]}
        # C++ independently checks all 24 points with long-double Newton 8/12.
        # Decimal adds selected opposite axial directions at distinct scales.
        for far in case['far']:
            with localcontext() as ctx:
                ctx.prec=120
                ro,zo=map(D.from_float,far['point'])
                distance=(ro**2+(zo-D.from_float(root['center'][2]))**2).sqrt()
                q=D.from_float(root['support_upper'])/distance
                general=D.from_float(6.67430e-8)*values[0]/distance*q**3/(1-q)
                assert D.from_float(far['tail_upper'])>=general
        for point_index in (1,5):
            far=case['far'][point_index];ro,zo=far['point']
            refs=[]
            for order,precision in ((12,80),(16,100)):
                with localcontext() as ctx:
                    ctx.prec=precision
                    value=sum((reference((*leaf['ring'],ro,zo),order,precision) for leaf in leaf_nodes),D(0))
                assert D.from_float(far['lower'])<=value<=D.from_float(far['upper']),(case['origin'],ro,zo,value,far)
                refs.append(value);newton_checks+=1
            row['farChecks'].append({'observer':[ro,zo],'lower':far['lower'],'upper':far['upper'],
                'generalTailUpper':far['tail_upper'],'evaluationWidth':far['evaluation_upper']-far['evaluation_lower'],
                'newton12Precision80':str(refs[0]),'newton16Precision100':str(refs[1]),
                'referenceDifferenceDiagnostic':str(abs(refs[0]-refs[1]))})
        rows.append(row);print('contained',bool(case['mixed']),case['origin'],flush=True)
    summary={'status':'PASS','scope':'Exact stored full-ring source geometry/moment companion and far diagnostic containment; not full physical geometry/RHS/RZ acceptance',
        'cases':len(rows),'nodes':total_nodes,'momentComparisons':total_checks,
        'decimalNewtonComparisons':newton_checks,'decimalPrecisions':[80,120],
        'probeSha256':hashlib.sha256(a.probe.read_bytes()).hexdigest(),
        'rows':rows,'limitations':['Newton quadrature convergence is diagnostic, not a certified reference error',
            'Production RZ values remain gated','No CUDA or long trajectory certification']}
    (out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n')
    print('RZ_RING_PARENT_DECIMAL_PASS',total_nodes,total_checks,newton_checks)

if __name__=='__main__':main()
