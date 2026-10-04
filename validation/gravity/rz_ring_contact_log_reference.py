#!/usr/bin/env python3
"""Arithmetic references and independent contact Duffy/Decimal AGM diagnostic.
No production kernel/interval code is imported. Duffy order differences are
diagnostics, not a reliable quadrature remainder or complete contact science.
"""
import argparse,json,subprocess
from decimal import Decimal as D,localcontext,getcontext
from pathlib import Path
from rz_ring_axis_reference import PI
from rz_ring_far_leaf_reference import gauss

def atan(x):
    if x<0:return -atan(-x)
    if x>1:return PI/2-atan(1/x)
    if x>D(".5"):return PI/4+atan((x-1)/(x+1))
    power=x;total=D(0)
    for k in range(2000):
        term=power/(2*k+1);total+=term if k%2==0 else -term
        if abs(term)<D(10)**(-getcontext().prec-5):return total
        power*=x*x
    raise RuntimeError("reference atan series incomplete")

def quadrant(a,b,precision):
    with localcontext() as c:
        c.prec=precision;a,b=map(D.from_float,(a,b))
        return a*b*((a*a+b*b).sqrt().ln()-D("1.5"))+(a*a*atan(b/a)+b*b*atan(a/b))/2

def duffy(values,order,precision,subtract_leading=False):
    with localcontext() as c:
        c.prec=precision
        rl,rh,zl,zh,ro,zo=map(D.from_float,values)
        if subtract_leading and not ro>0:
            raise ValueError("Leading contact subtraction requires a positive observer radius")
        rule=[((x+1)/2,w/2) for x,w in gauss(order)]
        integral=D(0)
        for a in (rl-ro,rh-ro):
            for b in (zl-zo,zh-zo):
                if a==0 or b==0:continue
                for triangle in (0,1):
                    for t,wt in rule:
                        for u,wu in rule:
                            rr=ro+t*a*(1 if triangle==0 else u)
                            zz=zo+t*b*(u if triangle==0 else 1)
                            s=((ro+rr)**2+(zo-zz)**2).sqrt()
                            q=((ro-rr)**2+(zo-zz)**2).sqrt()/s
                            aa,bb=D(1),q
                            for iteration in range(40):
                                na,nb=(aa+bb)/2,(aa*bb).sqrt()
                                aa,bb=na,nb
                                if abs(aa-bb)<D(10)**(-precision+8):break
                            else:raise RuntimeError("reference AGM incomplete")
                            k=PI/(2*aa)
                            integrand=t*rr*k/s
                            if subtract_leading:integrand+=t*t.ln()/2
                            integral+=abs(a*b)*wt*wu*integrand
                    if subtract_leading:integral+=abs(a*b)/8
        return integral

def call(binary,mode,values):
    p=subprocess.run([str(binary.resolve()),mode,*map(repr,values)],capture_output=True,text=True,check=True)
    return json.loads(p.stdout)

def main():
    a=argparse.ArgumentParser();a.add_argument('--probe',type=Path,required=True)
    a.add_argument('--output',type=Path,required=True)
    a.add_argument('--strict-contact',action='store_true');args=a.parse_args();rows=[]
    if args.strict_contact:
        contact=[]
        for ro,zo in ((1.,0.),(1.,.375),(.75,0.)):
            values=(.5,1.,-.375,.375,ro,zo)
            q=call(args.probe,'ring-enclosure-probe',
                   (.5,1.,-.375,.375,1.,ro,zo,16384,1e-10))
            assert q['status']==0
            # Exact singular coefficient at t=0 is r/s=1/2.
            # Integrate -(t/2)*log(t) analytically: 1/8 per triangle.
            refs=[]
            for n,p in ((64,80),(96,100)):
                integral=duffy(values,n,p,subtract_leading=True)
                with localcontext() as c:
                    c.prec=p;refs.append(-4*D.from_float(6.67430e-8)*integral)
            for v in refs:
                assert D.from_float(q['lower'])<=v<=D.from_float(q['upper']),(values,q,str(v))
            contact.append({'sourceObserverExactHex':[x.hex() for x in values],
                'enclosure':q,'duffy64Precision80':str(refs[0]),'duffy96Precision100':str(refs[1])})
            print('strict contact diagnostic contained',ro,zo,flush=True)
        args.output.parent.mkdir(parents=True,exist_ok=True)
        args.output.write_text(json.dumps({'status':'PASS','contactCases':3,'rows':contact,
            'singularSubtraction':'exact integral -(t/2)*log(t)=1/8 per triangle',
            'scope':'independent singularity-subtracted Duffy/Decimal AGM containment; differences not certified reference error or full RZ scientific acceptance'},indent=2)+'\n')
        return
    for x in (0.,1.e-300,1.e-20,.25,.5,1.,2.,1.e20,1.e300):
        bound=call(args.probe,'ring-atan-probe',(x,))
        for precision in (100,140):
            with localcontext() as c:
                c.prec=precision;v=atan(D.from_float(x))
                assert D.from_float(bound['lower'])<=v<=D.from_float(bound['upper']),(x,bound,str(v))
        rows.append({'primitive':'atan','inputHex':x.hex(),'enclosure':bound})
    for x in (1.e-300,1.e-20,.25,1.,2.,1.e20,1.e300):
        bound=call(args.probe,'ring-positive-log-probe',(x,))
        with localcontext() as c:
            c.prec=140;v=D.from_float(x).ln()
            assert D.from_float(bound['lower'])<=v<=D.from_float(bound['upper'])
        rows.append({'primitive':'log','inputHex':x.hex(),'enclosure':bound})
    for a,b in ((.25,.75),(1.,1.),(1.e-6,1.e-6),(.25,1.e-5)):
        bound=call(args.probe,'ring-quadrant-log-probe',(a,b))
        refs=[quadrant(a,b,p) for p in (100,140)]
        for v in refs:assert D.from_float(bound['lower'])<=v<=D.from_float(bound['upper'])
        rows.append({'primitive':'quadrant log','inputHex':[a.hex(),b.hex()],'enclosure':bound,
                     'reference100':str(refs[0]),'reference140':str(refs[1])})
    contact=[]
    for ro,zo in ((1.,0.),(1.,.001),(.9995,0.)):
        values=(.999,1.,-.001,.001,ro,zo)
        bound=call(args.probe,'ring-contact-log-probe',values)
        refs=[duffy(values,n,p) for n,p in ((24,80),(32,100))]
        for v in refs:
            assert D.from_float(bound['lower'])<=v<=D.from_float(bound['upper']),(values,bound,str(v))
        contact.append({'sourceObserverExactHex':[x.hex() for x in values],'enclosure':bound,
                        'duffy24Precision80':str(refs[0]),'duffy32Precision100':str(refs[1])})
        print('contact diagnostic contained',ro,zo,flush=True)
    result={'status':'PASS','primitiveCases':len(rows),'contactCases':len(contact),'rows':rows,
            'contact':contact,'scope':'arithmetic containment and Duffy/Decimal AGM diagnostic, not certified quadrature error or strict full-source contact acceptance'}
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(result,indent=2)+'\n')
    print('RZ_CONTACT_LOG_REFERENCE_PASS')
if __name__=='__main__':main()
