#!/usr/bin/env python3
"""Exact Fraction oracle for canonical stored-weight projection, not geometry.
No producer functions imported. Raw arrays remain in the local output folder.
"""
import argparse,json,subprocess
from fractions import Fraction as F
from pathlib import Path

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--probe',required=True,type=Path)
    ap.add_argument('--output',required=True,type=Path)
    a=ap.parse_args()
    q=subprocess.run([str(a.probe.resolve()),'projection-ledger-probe'],capture_output=True,text=True,check=True)
    a.output.parent.mkdir(parents=True,exist_ok=True)
    a.output.with_suffix('.raw.json').write_text(q.stdout)
    j=json.loads(q.stdout);assert j['negativePass']
    rows=[]
    for case in j['cases']:
        x=list(map(F.from_float,case['input']))
        w=list(map(F.from_float,case['weights']))
        mean=sum((wi*xi for wi,xi in zip(w,x)),F(0)) if case['periodic'] else F(0)
        errors=[abs(F.from_float(y)-(xi-mean)) for xi,y in zip(x,case['computed'])]
        assert all(e<=F.from_float(b) for e,b in zip(errors,case['cellBounds']))
        norm2=sum((wi*e*e for wi,e in zip(w,errors)),F(0))
        upper=F.from_float(case['normUpper'])
        assert norm2<=upper*upper
        if case['lane']==0:assert norm2==upper==0
        rows.append({'mixed':bool(case['mixed']),'periodic':bool(case['periodic']),
            'lane':case['lane'],'cells':len(x),'maxObservedAbsoluteError':float(max(errors)),
            'normUpper':case['normUpper'],'exactStoredWeightSum':str(sum(w,F(0)))})
    a.output.write_text(json.dumps({'status':'PASS','cases':rows,'negativePass':True,
        'scope':'canonical P_w=x-sum(stored weight*x), or identity for nonperiodic; physical density mean/source preprocessing and ideal geometry uncertified'},indent=2)+'\n')
    print('CONSTANT_MODE_PROJECTION_FRACTION_PASS',len(rows))
if __name__=='__main__':main()
