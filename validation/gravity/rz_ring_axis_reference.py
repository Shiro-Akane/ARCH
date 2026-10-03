"""Independent finite uniform annulus axis reference for RZ Core review.

Integrate the three-dimensional Newton kernel around the full azimuth:
Phi(0,z) = -2*pi*G*rho * integral_z' [sqrt(rR^2+u^2)-sqrt(rL^2+u^2)] dz'.
Here u=z'-z. This is a finite volume source, not a thin ring or 2-D log kernel.
No production GridMetrics, gravity kernel, EOS or AMR implementation is used.
"""
from decimal import Decimal, localcontext
import argparse
import json
import math

PI = Decimal("3.141592653589793238462643383279502884197169399375105820974944592307816406286208998628034825342117067982148086513282306647093844609550582231725359")
G = Decimal("6.67430e-8")

def decimal_value(value):
    return Decimal.from_float(value) if isinstance(value,float) else Decimal(str(value))

def axis_reference(r_lower,r_upper,z_lower,z_upper,density,z_observer,precision=80):
    args=[decimal_value(v) for v in (r_lower,r_upper,z_lower,z_upper,density,z_observer)]
    if not all(v.is_finite() for v in args):
        raise ValueError("Finite source/observer inputs required")
    rl,rr,zl,zr,rho,zo=args
    if not (0<=rl<rr and zl<zr and rho>0):
        raise ValueError("Positive density and ordered finite annulus required")
    with localcontext() as ctx:
        ctx.prec=precision
        def primitive(a,u):
            # Integral sqrt(a^2+u^2) du; continuous a=0 limit.
            if a==0:
                return u*abs(u)/2
            v=u/a
            signed_asinh=(abs(v)+(1+v*v).sqrt()).ln()
            if v<0:signed_asinh=-signed_asinh
            return (u*(a*a+u*u).sqrt()+a*a*signed_asinh)/2
        low,high=zl-zo,zr-zo
        def section(u):
            return (rr*rr+u*u).sqrt()-(rl*rl+u*u).sqrt()
        integral=(primitive(rr,high)-primitive(rr,low)
                  -primitive(rl,high)+primitive(rl,low))
        factor=2*PI*G*rho
        return {
            "potential":float(-factor*integral),
            "axial_acceleration":float(-factor*(section(high)-section(low))),
            "mass":float(PI*rho*(rr*rr-rl*rl)*(zr-zl)),
            "radial_acceleration":0.0,
        }

def tensor_quadrature(case,order):
    """Independent finite-volume Newton integration, bounded diagnostic only."""
    import numpy as np
    rl,rr,zl,zr,rho,zo=[float(case[k]) for k in
        ("r_lower","r_upper","z_lower","z_upper","density","z_observer")]
    nodes,weights=np.polynomial.legendre.leggauss(order)
    radial=rl+(nodes+1)*(rr-rl)/2
    axial=zl+(nodes+1)*(zr-zl)/2
    rw=weights*(rr-rl)/2
    zw=weights*(zr-zl)/2
    potential_terms=[];force_terms=[];mass_terms=[]
    for i,r in enumerate(radial):
        for j,z in enumerate(axial):
            u=z-zo;distance=math.hypot(r,u)
            measure=2*math.pi*r*float(rw[i])*float(zw[j])
            mass_terms.append(rho*measure)
            potential_terms.append(-float(G)*rho*measure/distance)
            force_terms.append(float(G)*rho*measure*u/(distance**3))
    return dict(potential=math.fsum(potential_terms),
                axial_acceleration=math.fsum(force_terms),
                mass=math.fsum(mass_terms),radial_acceleration=0.0)

CASES=[
 dict(name="solid_midplane",r_lower=0,r_upper=2,z_lower=-1,z_upper=1,density=3,z_observer=0),
 dict(name="solid_source_edge",r_lower=0,r_upper=2,z_lower=-1,z_upper=1,density=3,z_observer=1),
 dict(name="hollow_midplane",r_lower=1,r_upper=2,z_lower=-1,z_upper=1,density=3,z_observer=0),
 dict(name="solid_below",r_lower=0,r_upper=2,z_lower=-1,z_upper=1,density=3,z_observer=-3),
 dict(name="solid_above",r_lower=0,r_upper=2,z_lower=-1,z_upper=1,density=3,z_observer=3),
 dict(name="shifted_hollow",r_lower=1,r_upper=2,z_lower=4,z_upper=6,density=3,z_observer=8),
 dict(name="near_hollow_edge",r_lower="0.125",r_upper=1,z_lower=-1,z_upper=1,density=3,z_observer=1),
 dict(name="far_solid",r_lower=0,r_upper=2,z_lower=-1,z_upper=1,density=3,z_observer=100),
]

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output",required=True)
    args=parser.parse_args()
    rows=[]
    for case in CASES:
        parameters={k:v for k,v in case.items() if k!="name"}
        low=axis_reference(**parameters,precision=80)
        high=axis_reference(**parameters,precision=120)
        if low!=high:raise RuntimeError("Reference precision did not converge to same binary64")
        quadrature=[]
        for order in (8,16,32,64):
            actual=tensor_quadrature(case,order)
            errors={k:abs(actual[k]-high[k]) for k in high}
            quadrature.append(dict(order=order,values=actual,absolute_errors=errors))
        rows.append(dict(source=case,reference=high,quadrature=quadrature))
    report=dict(scope="Independent finite-volume axis reference only; no production RZ or scientific acceptance",
        G_cgs=str(G),potential_unit="cm^2/s^2",acceleration_unit="cm/s^2",mass_unit="g",
        normalization="full rotating volume, 2*pi",precision_digits=[80,120],rows=rows,
        limits=["axis observers only","axis source interior/contact use analytic continuous primitive, no epsilon",
                "uniform finite cell density","no selected production quadrature/order/opening or error budget",
                "no AMR/operator/CUDA/evolution coverage"])
    from pathlib import Path
    Path(args.output).write_text(json.dumps(report,indent=2)+"\n")
    print(json.dumps({"cases":len(rows),"reference_precision_converged":True,
                     "scope":report["scope"]}))

if __name__=="__main__":main()
