"""All-face force diagnostic from independent matched point Phi differences.
Not a contact-force certificate, spatial acceptance or Runtime evidence.
"""
import argparse
from decimal import Decimal as D,localcontext
from fractions import Fraction as F
import hashlib,json
from pathlib import Path
from rz_matched_native_reference import source_from_case
from rz_matched_source_reference import potential_reference,validate_source,G

def exact_uniform_union(source):
    """Exact integration-domain reduction only; never changes the native solver."""
    leaves=validate_source(source)
    rho=leaves[0]["density"]
    if any(v["density"]!=rho for v in leaves):return source,False
    keys=("r_lower","r_upper","z_lower","z_upper")
    values=[[F(v[k]) for k in keys] for v in leaves]
    rl=min(v[0] for v in values);rr=max(v[1] for v in values)
    zl=min(v[2] for v in values);zr=max(v[3] for v in values)
    # validate_source already rejects overlaps; equal area proves no uncovered
    # positive-area gap inside the common enclosing rectangle.
    assert sum(((b-a)*(d-c) for a,b,c,d in values),F(0))==(rr-rl)*(zr-zl)
    def dec(x):return D(x.numerator)/D(x.denominator)
    union=dict(sourceId=source["sourceId"],leaves=[dict(id="exact-uniform-union",
        r_lower=dec(rl),r_upper=dec(rr),z_lower=dec(zl),z_upper=dec(zr),density=rho)])
    return union,True

def audit(c):
    native=source_from_case(c)
    rows=[]
    with localcontext() as ctx:
        ctx.prec=100
        source,union=exact_uniform_union(native)
        ratio=D.from_float(c["source_identity"]["G"])/G
        for face in c["faces"]:
            axis=face["axis"]
            R,Z=map(lambda x:D.from_float(float(x)),face["center"][:2])
            actual=-D.from_float(float(face["gradient"]))
            if axis==0 and R==0:
                # Continuous full-circle reflection symmetry, not small-r cutoff.
                seq=[dict(kind="analytic-axis",force="0")]
                final=D(0);spread=D(0)
            else:
                ids=[i for i in (face["left"],face["right"]) if i>=0]
                width=min(F(c["spacing"][axis])/2**c["cells"][i]["level"] for i in ids)
                h0=D(width.numerator)/D(width.denominator)
                seq=[]
                for order,precision,divisor in ((16,80,8),(16,80,16),(16,80,32),
                                               (32,80,32),(32,100,32)):
                    with localcontext() as lane:
                        lane.prec=precision
                        h=h0/divisor
                        p=[R,Z];m=[R,Z];p[axis]+=h;m[axis]-=h
                        if m[0]<0:raise ValueError("Difference crossed axis")
                        plus=potential_reference(source,dict(r_observer=p[0],z_observer=p[1]),
                            order=order,precision=precision,t_panels=2)["potential"]
                        minus=potential_reference(source,dict(r_observer=m[0],z_observer=m[1]),
                            order=order,precision=precision,t_panels=2)["potential"]
                        force=-(plus-minus)/(2*h)*ratio
                        seq.append(dict(order=order,precision=precision,spacingDivisor=divisor,
                            displacement=str(h),force=str(force),absoluteDelta=str(abs(force-actual))))
                final=D(seq[-1]["force"])
                spread=abs(final-D(seq[-2]["force"]))
            fine=max((i for i in (face["left"],face["right"]) if i>=0),
                     key=lambda i:c["cells"][i]["level"])
            cell=c["cells"][fine]
            hr=F(c["spacing"][0])/2**cell["level"];hz=F(c["spacing"][1])/2**cell["level"]
            rl=F(c["origin"][0])+cell["index"][0]*hr
            area=2*F(float(face["center"][0]))*hz if axis==0 else (rl+hr)**2-rl**2
            rows.append(dict(faceIndex=face["index"],axis=axis,boundary=face["boundary_side"]>=0,
                coarseFine=face["left"]>=0 and face["right"]>=0 and
                    c["cells"][face["left"]]["level"]!=c["cells"][face["right"]]["level"],
                actualNormalAcceleration=str(actual),referenceSequence=seq,
                finalAbsoluteDelta=str(abs(final-actual)),precisionDelta=str(spread),
                areaWithoutCommonPiNumerator=area.numerator,areaWithoutCommonPiDenominator=area.denominator,
                certified=False))
            print("face",c["radial_origin"],c["mixed"],face["index"],"processed",flush=True)
        total=sum((D(v["areaWithoutCommonPiNumerator"])/D(v["areaWithoutCommonPiDenominator"]) for v in rows),D(0))
        squared=sum((D(v["areaWithoutCommonPiNumerator"])/D(v["areaWithoutCommonPiDenominator"])*
            D(v["finalAbsoluteDelta"])**2 for v in rows),D(0))
        return dict(radialOrigin=c["radial_origin"],mixedAmr=bool(c["mixed"]),sourceId=native["sourceId"],
            sourceIdentity=c["source_identity"],nativeLeaves=len(native["leaves"]),
            referenceExactUniformUnion=union,faces=len(rows),coarseFineFaces=sum(v["coarseFine"] for v in rows),
            faceAreaRmsNormalAccelerationDelta=str((squared/total).sqrt()),
            maximumNormalAccelerationDelta=str(max(D(v["finalAbsoluteDelta"]) for v in rows)),
            rows=rows)

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--probe-record",type=Path,required=True)
    p.add_argument("--output",type=Path,required=True);a=p.parse_args()
    if a.output.exists():p.error("output must be new")
    record=json.loads(a.probe_record.read_text())
    rows=[audit(c) for c in record["cases"]]
    result=dict(status="ALL_FACE_DIAGNOSTIC_NOT_SCIENTIFIC_ACCEPTANCE",
        probeRecordSha256=hashlib.sha256(a.probe_record.read_bytes()).hexdigest(),
        units=dict(potential="cm^2/s^2",acceleration="cm/s^2"),rows=rows,
        method="negative centered derivative of independent matched Decimal Phi; axis analytic; frozen step/order/precision sequence",
        limitations=["Difference, quadrature and precision variation are estimates, not certified bounds",
            "Exact uniform union changes only equivalent reference integration domain; all native cells/faces/source identity retained",
            "No approved continuous force threshold is inferred from observed errors",
            "Static native numerical probe, not Runtime evolution or CUDA release"])
    a.output.parent.mkdir(parents=True,exist_ok=True)
    a.output.write_text(json.dumps(result,indent=2)+"\n")
    print("MATCHED_ALL_FACE_FORCE_DIAGNOSTIC",sum(v["faces"] for v in rows))
if __name__=="__main__":main()
