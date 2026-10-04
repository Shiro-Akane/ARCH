#!/usr/bin/env python3
"""Independent Decimal physical pi/source and exact stored-weight norm check.
Does not import the production source/interval routines. Arrays stay local.
"""
import argparse,json,subprocess
from decimal import Decimal as D,localcontext
from pathlib import Path
from rz_ring_axis_reference import PI

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--probe',type=Path,required=True)
    ap.add_argument('--output',type=Path,required=True);a=ap.parse_args()
    q=subprocess.run([str(a.probe.resolve()),'gravity-source-bounds-probe'],capture_output=True,text=True,check=True)
    a.output.parent.mkdir(parents=True,exist_ok=True);a.output.with_suffix('.raw.json').write_text(q.stdout)
    j=json.loads(q.stdout);assert j['negativePass']
    rows=[]
    for p in (100,140):
        with localcontext() as c:
            c.prec=p
            G=D.from_float(6.67430e-8)
            errors=[]
            for i,rho in enumerate(j['density']):
                exact=-4*PI*G*D.from_float(rho)
                actual=D.from_float(j['source'][i])
                assert D.from_float(j['lower'][i])<=exact<=D.from_float(j['upper'][i]),(i,str(exact))
                e=abs(actual-exact);errors.append(e)
                assert e<=D.from_float(j['cellBounds'][i]),(i,str(e))
                if p==140:
                    rows.append({'densityExactHex':float(rho).hex(),
                        'source':j['source'][i],'lower':j['lower'][i],'upper':j['upper'][i],
                        'absoluteErrorBound':j['cellBounds'][i],
                        'referenceDecimal140':str(exact),'observedAbsoluteError':str(e)})
                if rho==0.:assert actual==exact==0 and j['cellBounds'][i]==0
            square=sum(D.from_float(w)*e*e for w,e in zip(j['weights'],errors))
            assert square<=D.from_float(j['normUpper'])**2
    a.output.write_text(json.dumps({'status':'PASS','cells':len(rows),'precision':[100,140],
        'GExactHex':(6.67430e-8).hex(),'rows':rows,'normUpperStoredWeights':j['normUpper'],
        'scope':'isolated -4*pi*G*rho with stored rho/shared G, mathematical pi; norm conditional on stored native weights; not periodic or full RZ science'},indent=2)+'\n')
    print('ISOLATED_GRAVITY_SOURCE_DECIMAL_PASS',len(rows))
if __name__=='__main__':main()
