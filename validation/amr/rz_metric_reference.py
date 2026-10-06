"""Verify full-rotation RZ metric fixtures from independent Decimal integrals.

No production metric implementation is imported. This is a measure oracle,
not scientific acceptance of the whole RZ solver or a new physical tolerance.
"""
import argparse
from decimal import Decimal, localcontext
import json
from pathlib import Path
import re

ROOT=Path(__file__).resolve().parents[2]
PI=Decimal("3.141592653589793238462643383279502884197169399375105820974944592307816406286208998628034825342117067982148086513282306647093844609550582231725359")

def references(inputs,digits):
    with localcontext() as ctx:
        ctx.prec=digits
        left,right,dz=map(Decimal.from_float,inputs)
        if not 0<=left<right or dz<=0:
            raise ValueError("Invalid finite-volume fixture")
        axial_area=PI*(right**2-left**2)
        return [float(axial_area*dz),float(2*PI*left*dz),
                float(2*PI*right*dz),float(axial_area)]

def check():
    text=(ROOT/"tests/math/geometry/RzMetricCases.h").read_text()
    body=text.split("// BEGIN INDEPENDENT RZ MEASURE DATA",1)[1].split(
        "// END INDEPENDENT RZ MEASURE DATA",1)[0]
    rows=re.findall(r"\{([^{}]+)\},",body)
    if not rows:raise ValueError("Missing measure fixtures")
    evidence=[]
    for i,row in enumerate(rows):
        data=[float.fromhex(value.strip()) for value in row.split(",")]
        if len(data)!=7:raise ValueError("Invalid row length")
        low,high=references(data[:3],70),references(data[:3],100)
        if low!=high or high!=data[3:]:
            raise ValueError(f"Independent reference mismatch at row {i}")
        evidence.append(dict(inputs=data[:3],reference=high))
    return dict(status="pass",cases=len(rows),precision_digits=[70,100],
        scope="Full-rotation metric fixture verification only",
        production_rz_enabled=False,scientific_acceptance=False,rows=evidence)

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output")
    args=parser.parse_args()
    result=check()
    if args.output:Path(args.output).write_text(json.dumps(result,indent=2)+"\n")
    print(json.dumps({k:v for k,v in result.items() if k!="rows"}))

if __name__=="__main__":main()
