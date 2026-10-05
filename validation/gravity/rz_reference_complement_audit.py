"""Arithmetic audit of direct near-contact complement; no science threshold."""
import argparse
from decimal import Decimal as D, localcontext
import hashlib
import json
from pathlib import Path
from rz_ring_offaxis_reference import elliptic_ke, elliptic_ke_complement, finite_volume_reference, contact_potential_reference, CASES, CONTACT_CASES
from rz_ring_k_interval_reference import atan_reciprocal

def independent_k(q, precision):
    with localcontext() as ctx:
        ctx.prec=precision
        pi=16*atan_reciprocal(5,precision)-4*atan_reciprocal(239,precision)
        a,b=D(1),q.sqrt()
        for _ in range(80):
            a,b=(a+b)/2,(a*b).sqrt()
            if abs(a-b)<D(10)**(-precision+5)*a:
                return pi/(2*a)
    raise ArithmeticError("high precision K audit did not converge")

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--output",required=True,type=Path)
    a=p.parse_args()
    if a.output.exists():p.error("output must be new")
    rows=[]
    for q in map(D,["1",".5",".25","1e-20","1e-100","1e-300"]):
        target=independent_k(q,180)
        sequence=[]
        for precision in (40,80,120):
            with localcontext() as ctx:
                ctx.prec=precision
                k,e=elliptic_ke_complement(q)
                relative=abs(k-target)/target
                # Arithmetic guard tied to requested digits, not a physical gate.
                assert relative<D(10)**(-precision+8),(q,precision,relative)
                sequence.append(dict(precision=precision,K=str(k),E=str(e),
                    kRelativeArithmeticDelta=str(relative),oneMinusQCollapsed=1-q==1))
        with localcontext() as ctx:
            ctx.prec=140
            for item in sequence:
                assert abs(D(item["E"])-D(sequence[-1]["E"]))<D(10)**(-item["precision"]+8)
        rows.append(dict(complement=str(q),sequence=sequence))
    rejected=0
    for q in map(D,["0","-1","1.01","NaN","Infinity"]):
        try:elliptic_ke_complement(q)
        except ValueError:rejected+=1
        else:raise AssertionError("invalid complement accepted")
    with localcontext() as ctx:
        ctx.prec=80
        R=D(1);dr=D("1e-60");dz=D("2e-60");radius=R+dr
        s2=(R+radius)**2+dz*dz;d2=dr*dr+dz*dz
        m=4*R*radius/s2
        assert m==1 and d2/s2>0
        try:elliptic_ke(m)
        except ValueError:old_rejected=True
        else:raise AssertionError("old cancellation counterexample missing")
        k,e=elliptic_ke_complement(d2/s2)
        assert k.is_finite() and e.is_finite()
    # Actual integrator consumers, independent changes in arithmetic precision.
    consumer=[]
    for case in (CASES[0],CASES[4]):
        low=finite_volume_reference(case,8,60)
        high=finite_volume_reference(case,8,100)
        assert all(float(low[key])==float(high[key]) for key in high)
        consumer.append(dict(name=case["name"],kind="exterior Phi/force",order=8,
            binary64PrecisionAgreement=True,certified=False))
    for case in CONTACT_CASES:
        low=contact_potential_reference(case,8,60,1)
        high=contact_potential_reference(case,8,100,1)
        assert float(low["potential"])==float(high["potential"])
        consumer.append(dict(name=case["name"],kind="contact Phi only",order=8,
            binary64PrecisionAgreement=True,certified=False))
    result=dict(status="ARITHMETIC_PASS_NOT_SCIENTIFIC_ACCEPTANCE",rows=rows,
        rejectedInvalidComplements=rejected,
        counterexample=dict(dr="1e-60",dz="2e-60",precision=80,oldParameterRoundedToOne=old_rejected,
            directComplementPositive=True,K=str(k),E=str(e)),
        consumerChecks=consumer,
        sourceSha256=hashlib.sha256(Path(__file__).with_name("rz_ring_offaxis_reference.py").read_bytes()).hexdigest(),
        limitations=["K cross-check uses independent Machin pi and higher precision AGM; not quadrature certification",
            "E arithmetic precision agreement is not an independent force oracle",
            "Same quadrature order comparisons check arithmetic only",
            "No contact-force, continuous spatial accuracy, Core change, CUDA or evolution grant"])
    a.output.parent.mkdir(parents=True,exist_ok=True)
    a.output.write_text(json.dumps(result,indent=2)+"\n")
    print("RZ_REFERENCE_COMPLEMENT_ARITHMETIC_PASS cases=6 invalid=5 consumers=5")
if __name__=="__main__":main()
