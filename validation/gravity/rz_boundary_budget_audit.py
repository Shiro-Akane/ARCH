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
from rz_ring_axis_reference import PI

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
        signed_rows=[F(0) for _ in cells]
        for face in c["faces"]:
            if face["boundary_side"]<0:continue
            _,boundary_coefficient,center=reconstruct(c,face,neighbors)
            owner=face["left"] if face["left"]>=0 else face["right"]
            area=2*center[0]*width(owner,1) if face["axis"]==0 else radial(owner)
            for side,cell in enumerate((face["left"],face["right"])):
                if cell>=0:
                    mapping=(1 if side==0 else -1)*area/volume[cell]*boundary_coefficient
                    signed_rows[cell]+=mapping
                    absolute_rows[cell]+=abs(mapping)
        norm2=sum((v*x*x for v,x in zip(volume,absolute_rows)),F(0))/sum(volume)
        assert norm2>0
        if c.get("dynamic_budget"):
            assert len(c["sensitivity_cells"])==len(cells)
            for exact,upper in zip(absolute_rows,c["sensitivity_cells"]):
                assert exact<=F(upper)
            assert norm2<=F(c["boundary_sensitivity_upper"])**2
            closest=[F(0) if F(lo)<=0<=F(hi) else min(abs(F(lo)),abs(F(hi)))
                for lo,hi in zip(c["source_lower"],c["source_upper"])]
            source_lower2=sum((v*x*x for v,x in zip(volume,closest)),F(0))/sum(volume)
            assert F(c["proposal_source_norm_lower"])**2<=source_lower2
            norm_basis=F(c["proposal_source_norm_lower"])
            if c.get("proposal_basis")==1:
                assert all(x>=0 for x in signed_rows)
                edges=[(F(c["origin"][0])+cell["index"][0]*width(i,0),
                    F(c["origin"][0])+(cell["index"][0]+1)*width(i,0),
                    F(c["origin"][1])+cell["index"][1]*width(i,1),
                    F(c["origin"][1])+(cell["index"][1]+1)*width(i,1))
                    for i,cell in enumerate(cells)]
                outer=max(x[1] for x in edges);span=max(x[3] for x in edges)-min(x[2] for x in edges)
                distance=F(c["proposal_distance_upper"])
                assert distance>0 and distance**2>=(2*outer)**2+span**2
                mass=F(c["proposal_mass_lower"]);potential=F(c["proposal_potential_magnitude_lower"])
                assert potential<=F(6.6743e-8)*mass/distance
                for precision in (100,140):
                    with localcontext() as ctx:
                        ctx.prec=precision
                        exact_mass=PI*sum(Decimal(v.numerator)/Decimal(v.denominator)*
                            Decimal.from_float(cell["density"]) for v,cell in zip(volume,cells))
                        assert Decimal.from_float(c["proposal_mass_lower"])<=exact_mass
                rhs_min=[]
                for source_min,mapping,value in zip(closest,signed_rows,c["proposal_rhs_cell_lower"]):
                    reference=source_min+potential*mapping
                    assert F(value)<=reference
                    rhs_min.append(reference)
                rhs_lower2=sum((v*x*x for v,x in zip(volume,rhs_min)),F(0))/sum(volume)
                norm_basis=F(c["proposal_rhs_norm_lower"])
                assert norm_basis**2<=rhs_lower2
            assert F(c["proposal_tolerance"])<=F(1e-10)*norm_basis
            assert F(c["face_target"])*F(c["boundary_sensitivity_upper"])<=F(c["proposal_tolerance"])/2
        with localcontext() as ctx:
            ctx.prec=100
            coefficient=(Decimal(norm2.numerator)/Decimal(norm2.denominator)).sqrt()
            tolerance=Decimal.from_float(c.get("tolerance_safe",c.get("proposal_tolerance",0.)))
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
            "recordedToleranceSafePerSecondSquared":c.get("tolerance_safe"),
            "budgetOnly":bool(c.get("budget_only",False)),
            "halfToleranceFaceBudgetCmSquaredPerSecondSquaredDiagnostic":str(proposal),
            "fixedTargetScaleDiagnostics":scales,
            "dynamicProposalExactChecks":bool(c.get("dynamic_budget",False)),
            "recordedSolvePassed":None if c.get("budget_only") else c["total_residual_upper"]<=c["tolerance_safe"]})
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
