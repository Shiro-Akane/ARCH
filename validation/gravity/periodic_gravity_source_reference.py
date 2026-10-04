#!/usr/bin/env python3
"""Independent normalized stored-weight density contrast + physical pi oracle."""
import argparse,json,subprocess
from decimal import Decimal as D,localcontext
from fractions import Fraction as F
from pathlib import Path
from rz_ring_axis_reference import PI

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--probe',required=True,type=Path)
    ap.add_argument('--output',required=True,type=Path);a=ap.parse_args()
    q=subprocess.run([str(a.probe.resolve()),'periodic-source-bounds-probe'],capture_output=True,text=True,check=True)
    a.output.parent.mkdir(parents=True,exist_ok=True)
    a.output.with_suffix('.raw.json').write_text(q.stdout)
    j=json.loads(q.stdout);assert j['negativePass'];rows=[]
    for case in j['cases']:
        rho=list(map(F.from_float,case['density']));w=list(map(F.from_float,case['weights']))
        mean=sum((ri*wi for ri,wi in zip(rho,w)),F(0))/sum(w,F(0))
        for precision in (100,140):
            with localcontext() as c:
                c.prec=precision;errors=[];max_reference=D(0)
                for i,ri in enumerate(rho):
                    contrast=ri-mean
                    exact=-4*PI*D.from_float(6.67430e-8)*D(contrast.numerator)/D(contrast.denominator)
                    assert D.from_float(case['lower'][i])<=exact<=D.from_float(case['upper'][i])
                    error=abs(D.from_float(case['source'][i])-exact)
                    assert error<=D.from_float(case['cellBounds'][i]);errors.append(error)
                    max_reference=max(max_reference,abs(exact))
                norm2=sum(D.from_float(wi)*e*e for wi,e in zip(case['weights'],errors))
                assert norm2<=D.from_float(case['normUpper'])**2
                if case['lane']==0:assert norm2==case['normUpper']==0
                if case['lane']==5:
                    assert max_reference>0 and case['normUpper']>0
                    assert all(x==0. for x in case['source'])
                if precision==140:
                    rows.append({'mixed':bool(case['mixed']),'lane':case['lane'],
                        'path':['corrected host CompositeExecution difference_scale/mean/project','scalar subtract-first/project','legacy multiply-before-subtract diagnostic'][case['path']],
                        'cells':len(rho),'maxObservedAbsoluteError':str(max(errors)),
                        'maxReferenceMagnitude':str(max_reference),'normUpper':case['normUpper']})
    a.output.write_text(json.dumps({'status':'PASS','precision':[100,140],'cases':rows,
        'scope':'-4 mathematical pi * stored shared G * normalized stored-weight density contrast; actual final arrays; ideal geometry and lifecycle uncertified'},indent=2)+'\n')
    print('PERIODIC_SOURCE_DECIMAL_FRACTION_PASS',len(rows))
if __name__=='__main__':main()
