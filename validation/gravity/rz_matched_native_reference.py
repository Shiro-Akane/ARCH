"""Consume explicit source/geometry/stamp from actual solved native probe.
Actual boundary point-potential diagnostics, including Duffy contact estimates.\nNo contact-force oracle or scientific PASS.
Raw field arrays stay local; no density inference from Poisson RHS.
"""
import argparse
import hashlib
import json
from decimal import Decimal, localcontext
from fractions import Fraction
from pathlib import Path
from rz_matched_source_reference import potential_reference, validate_source, G

def source_from_case(case):
    identity=case["source_identity"]
    for key in ("topology","operator_revision","boundary_revision","accuracy_revision","generation"):
        value=identity[key]
        if type(value) is not int or value <= 0:
            raise ValueError("Missing/nonpositive identity revision")
    if not identity["inputs"]:
        raise ValueError("Missing source dependency")
    for item in identity["inputs"]:
        if item["epoch"] != identity["topology"] or item["slot"] not in (0,1,2):
            raise ValueError("Source topology/slot mismatch")
        for key in ("uid","version","storage_generation"):
            if type(item[key]) is not int or item[key] <= 0:
                raise ValueError("Invalid source dependency")
    for key in ("time","G"):
        if not Decimal.from_float(float(identity[key])).is_finite():
            raise ValueError("Nonfinite source stamp")
    if identity["G"] <= 0:raise ValueError("Invalid G")
    leaves=[]
    for i,cell in enumerate(case["cells"]):
        edges=cell["edges"]  # explicit actual bounds; never infer density
        if len(edges)!=4:raise ValueError("Missing leaf bounds")
        for axis in range(2):
            h=Fraction(case["spacing"][axis])/2**cell["level"]
            for side in range(2):
                ideal=Fraction(case["origin"][axis])+(cell["index"][axis]+side)*h
                if Fraction(edges[2*axis+side])!=ideal:
                    raise ValueError("Rounded root/leaf geometry not certified by this adapter")
        leaves.append(dict(id=str(i),r_lower=edges[0],r_upper=edges[1],
                           z_lower=edges[2],z_upper=edges[3],density=cell["density"]))
    source=dict(sourceId=hashlib.sha256(json.dumps(dict(identity=identity,leaves=leaves),
                 sort_keys=True,allow_nan=False).encode()).hexdigest(),leaves=leaves)
    validate_source(source)
    return source

def audit_case(case,precision=80,order=16,t_panels=1):
    source=source_from_case(case)
    rows=[]
    with localcontext() as ctx:
        ctx.prec=precision
        ratio=Decimal.from_float(float(case["source_identity"]["G"]))/G
        for face in case["faces"]:
            if face["boundary_side"]<0:continue
            observer=dict(r_observer=face["center"][0],z_observer=face["center"][1])
            values=potential_reference(source,observer,order=order,
                                       precision=precision,t_panels=t_panels)
            phi=values["potential"]*ratio
            actual_phi=Decimal.from_float(float(case["face_values"][face["index"]]))
            rows.append(dict(faceIndex=face["index"],
                potentialAbsoluteDelta=str(abs(phi-actual_phi)),
                referencePotentialDecimal=str(phi),
                contactLeaves=values["contactLeaves"],
                exteriorLeaves=values["exteriorLeaves"],
                axisLeaves=values["axisLeaves"],certified=False))
    return dict(radialOrigin=case["radial_origin"],mixedAmr=bool(case["mixed"]),
        sourceId=source["sourceId"],sourceIdentity=case["source_identity"],
        cells=len(case["cells"]),boundaryObservers=len(rows),rows=rows)

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--probe-record",required=True,type=Path)
    p.add_argument("--output",required=True,type=Path)
    p.add_argument("--order",type=int,default=16)
    p.add_argument("--precision",type=int,default=80)
    p.add_argument("--t-panels",type=int,default=1)
    a=p.parse_args()
    if a.output.exists():p.error("output must be new")
    data=json.loads(a.probe_record.read_text())
    rows=[audit_case(c,a.precision,a.order,a.t_panels) for c in data["cases"]]
    result=dict(status="BOUNDARY_POTENTIAL_DIAGNOSTIC_NOT_SCIENTIFIC_ACCEPTANCE",
        order=a.order,precision=a.precision,tPanels=a.t_panels,
        probeRecordSha256=hashlib.sha256(a.probe_record.read_bytes()).hexdigest(),
        rows=rows,units=dict(potential="cm^2/s^2",acceleration="cm/s^2"),
        GSemantics="Analytic Decimal reference rescaled to exact input FP64 G before comparison",
        limitations=["Actual static numerical density/stamp, not Runtime all-block stage/regrid publication",
                     "Boundary point Phi estimate only; continuous face force and cell Phi not covered",
                     "No certified reference error or new threshold; original continuous science gates remain"])
    a.output.parent.mkdir(parents=True,exist_ok=True)
    a.output.write_text(json.dumps(result,indent=2)+"\n")
    print("MATCHED_NATIVE_BOUNDARY_PHI_DIAGNOSTIC",sum(r["boundaryObservers"] for r in rows))
if __name__=="__main__":main()
