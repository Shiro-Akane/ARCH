#!/usr/bin/env python3
"""Independent full 3D Newton product quadrature diagnostic.
No production K, moments, or tail formulas are imported. Two orders and
Decimal precisions diagnose reference stability; they do not certify quadrature
error or replace the analytical Legendre proof and production science gates.
"""
import argparse,json,subprocess
from decimal import Decimal as D,localcontext
from pathlib import Path
from rz_ring_axis_reference import PI

def cosine(x):
    term=total=D(1)
    for k in range(1,200):
        term *= -x*x/D((2*k-1)*(2*k))
        total += term
        if abs(term)<D(10)**(-__import__('decimal').getcontext().prec-5):return total
    raise RuntimeError("cosine series did not converge")

def gauss(n):
    import math
    rows=[]
    for i in range(n):
        x=D.from_float(math.cos(math.pi*(i+.75)/(n+.5)))
        for iteration in range(30):
            p0,p1=D(1),x
            for k in range(2,n+1):
                p0,p1=p1,((2*k-1)*x*p1-(k-1)*p0)/k
            derivative=n*(x*p1-p0)/(x*x-1)
            step=p1/derivative;x-=step
            if abs(step)<D(10)**(-__import__('decimal').getcontext().prec+8):break
        else:raise RuntimeError("Gauss root did not converge")
        rows.append((x,2/((1-x*x)*derivative*derivative)))
    return rows

def reference(values,n,precision):
    with localcontext() as ctx:
        ctx.prec=precision
        rl,rh,zl,zh,rho,ro,zo=map(D.from_float,values)
        rule=gauss(n)
        angles=[(cosine(PI*x),PI*w) for x,w in rule]
        radial=[((rh+rl)/2+(rh-rl)*x/2,(rh-rl)*w/2) for x,w in rule]
        axial=[((zh+zl)/2+(zh-zl)*x/2,(zh-zl)*w/2) for x,w in rule]
        total=D(0)
        for rr,wr in radial:
            for zz,wz in axial:
                for c,wa in angles:
                    distance=(rr*rr+ro*ro-2*rr*ro*c+(zz-zo)**2).sqrt()
                    total+=rr*wr*wz*wa/distance
        return -D.from_float(6.67430e-8)*rho*total

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--probe',type=Path,required=True)
    ap.add_argument('--output',type=Path,required=True);a=ap.parse_args()
    rows=[]
    for rl in (0.,.5):
        for distance in (1.e3,1.e6,1.e12):
            for dr,dz in ((1.,0.),(.6,.8),(.6,-.8)):
                values=(rl,1.,-.23,.71,1.,distance*dr,distance*dz)
                q=subprocess.run([str(a.probe.resolve()),'ring-enclosure-probe',
                    *map(repr,values), '1','1e-10'],capture_output=True,text=True,check=True)
                bound=json.loads(q.stdout)
                assert bound['status']==0 and bound['range_evaluations']==0
                refs=[reference(values,n,p) for n,p in ((12,80),(16,100))]
                for v in refs:
                    assert D.from_float(bound['lower'])<=v<=D.from_float(bound['upper']),(values,bound,str(v))
                rows.append({'exactInputHex':[x.hex() for x in values],
                    'newtonOrder12Precision80':str(refs[0]),
                    'newtonOrder16Precision100':str(refs[1]),'enclosure':bound})
                print('contained',rl,distance,dr,dz,flush=True)
    result={'status':'PASS','cases':len(rows),'reference':'independent full-azimuth 3D Newton product quadrature',
        'orders':[12,16],'decimalPrecisions':[80,100],
        'scope':'diagnostic containment, not certified quadrature error or full RZ scientific acceptance','rows':rows}
    a.output.parent.mkdir(parents=True,exist_ok=True)
    a.output.write_text(json.dumps(result,indent=2)+'\n')
    print('RZ_FAR_LEAF_NEWTON_DECIMAL_PASS')
if __name__=='__main__':main()
