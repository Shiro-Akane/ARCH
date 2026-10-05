#!/usr/bin/env python3
"""Offline exact native B sensitivity for existing solved ring records.
Diagnostic policy preparation only: Decimal sqrt is not an outward certificate.
Never changes a source, solve control, physical threshold or original record.
"""
import argparse, hashlib, json
from decimal import Decimal, localcontext
from fractions import Fraction as F
from pathlib import Path
from rz_stencil_construction_reference import reconstruct

def audit(path):
    record=json.loads(path.read_text())
    rows=[]
    for c in record["cases"]:
        cells=c["cells"];neighbors=[set() for _ in cells]
        for face in c["faces"]:
            if face["left"]>=0 and face["right"]>=0:
                neighbors[face["left"]].add(face["right"])
                neighbors[face["right"]].add(face["left"])
        def width(i,a):return F(c["spacing"][a])/2**cells[i]["level"]
        def radial(i):
            lo=F(c["origin"][0])+cells[i]["index"][0]*width(i,0)
            return (lo+width(i,0))**2-lo**2
        volume=[radial(i)*width(i,1) for i in range(len(cells))]
        absolute_rows=[F(0) for _ in cells]
        for face in c["faces"]:
            if face["boundary_side"]<0:continue
            _,boundary_coefficient,center=reconstruct(c,face,neighbors)
            owner=face["left"] if face["left"]>=0 else face["right"]
            area=2*center[0]*width(owner,1) if face["axis"]==0 else radial(owner)
            for cell in (face["left"],face["right"]):
                if cell>=0:absolute_rows[cell]+=abs(area/volume[cell]*boundary_coefficient)
        norm2=sum((v*x*x for v,x in zip(volume,absolute_rows)),F(0))/sum(volume)
        assert norm2>0
        with localcontext() as ctx:
            ctx.prec=100
            coefficient=(Decimal(norm2.numerator)/Decimal(norm2.denominator)).sqrt()
            tolerance=Decimal.from_float(c["tolerance_safe"])
            proposal=tolerance/(2*coefficient)
            fixed=Decimal("1e-18")*coefficient
            scales=[]
            for scale in ("1e-6","1","1e6"):
                factor=Decimal(scale)
                scales.append({"densityScale":scale,
                    "linearScalingDiagnosticOnly":True,
                    "scaledHalfTolerance":str(tolerance*factor/2),
                    "fixedTargetResidualContribution":str(fixed),
                    "fixedContributionOverHalfTolerance":str(fixed/(tolerance*factor/2))})
        rows.append({"radialOrigin":c["radial_origin"],"mixed":bool(c.get("mixed",False)),
            "cells":len(cells),"boundaryRowsNormSquaredExact":
                {"numerator":str(norm2.numerator),"denominator":str(norm2.denominator)},
            "boundarySensitivityCmMinus2":str(coefficient),
            "recordedToleranceSafePerSecondSquared":c["tolerance_safe"],
            "halfToleranceFaceBudgetCmSquaredPerSecondSquaredDiagnostic":str(proposal),
            "fixedTargetScaleDiagnostics":scales,
            "recordedSolvePassed":c["total_residual_upper"]<=c["tolerance_safe"]})
    return {"recordSha256":hashlib.sha256(path.read_bytes()).hexdigest(),"rows":rows}

def main():
    p=argparse.ArgumentParser()
    p.add_argument("--record",type=Path,action="append",required=True)
    p.add_argument("--output",type=Path,required=True)
    a=p.parse_args()
    if a.output.exists():p.error("output must be new")
    result={"status":"OFFLINE_EXACT_MAPPING_DIAGNOSTIC",
        "records":[audit(path) for path in a.record],
        "meaning":"K=norm_native(abs(B)*1); potential target delta contributes at most K*delta in exact native arithmetic",
        "limitations":["Decimal sqrt/result is diagnostic, not an outward runtime certificate",
            "Scaled density rows are linear mathematical diagnostics, not new physical runs",
            "No new thresholds or production budget selected",
            "Runtime must use original owner interval K and final complete original-request assessment",
            "No Runtime source/solve/publication, continuous Phi/force or long-run qualification"]}
    a.output.parent.mkdir(parents=True,exist_ok=True)
    a.output.write_text(json.dumps(result,indent=2)+"\n")
    print("RZ_BOUNDARY_BUDGET_AUDIT cases="+str(sum(len(x["rows"]) for x in result["records"])))
if __name__=="__main__":main()
