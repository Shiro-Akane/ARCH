"""Stored-field face export audit; not an independent physical force oracle."""
import argparse,hashlib,json,math,struct
from fractions import Fraction as F
from pathlib import Path

def bitvalue(x):return struct.pack(">d",x)
def anchored(c,f):
    anchor=f["left"] if f["left"]>=0 else f["right"]
    phi=c["potential"];boundary=c["face_values"][f["index"]] if f["boundary_side"]>=0 else 0.
    pairs=[(co,phi[i],phi[anchor]) for i,co in zip(f["samples"],f["coefficients"])]
    pairs.append((f["boundary_coefficient"],boundary,phi[anchor]))
    if not all(math.isfinite(x) for p in pairs for x in p):
        raise ValueError("nonfinite stencil input")
    exact=sum((F(co)*(F(x)-F(y)) for co,x,y in pairs),F(0))
    total=correction=0.
    for co,x,y in pairs:
        term=co*(x-y);nxt=total+term
        correction+=(total-nxt)+term if abs(total)>=abs(term) else (term-nxt)+total
        total=nxt
    actual=total+correction
    if not math.isfinite(actual):raise ValueError("nonfinite gradient")
    return actual,exact

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--probe-record",required=True,type=Path)
    p.add_argument("--output",required=True,type=Path);a=p.parse_args()
    if a.output.exists():p.error("output must be new")
    rows=[];counterexamples=0;failures=[]
    for c in json.loads(a.probe_record.read_text())["cases"]:
        errors=[];boundary=interface=0
        for f in c["faces"]:
            if len(f["samples"])!=len(f["coefficients"]):raise ValueError("coefficient extent")
            value,exact=anchored(c,f)
            if bitvalue(value)!=bitvalue(f["gradient"]):
                failures.append(dict(radialOrigin=c["radial_origin"],mixed=c["mixed"],face=f["index"]))
            errors.append(abs(F(f["gradient"])-exact))
            if value!=0 and bitvalue(-value)!=bitvalue(f["gradient"]):counterexamples+=1
            boundary+=f["boundary_side"]>=0
            interface+=f["left"]>=0 and f["right"]>=0 and c["cells"][f["left"]]["level"]!=c["cells"][f["right"]]["level"]
        rows.append(dict(radialOrigin=c["radial_origin"],mixedAmr=bool(c["mixed"]),
            cells=len(c["cells"]),faces=len(c["faces"]),boundaryFaces=boundary,coarseFineFaces=interface,
            maximumStoredStencilArithmeticDelta=float(max(errors)),
            nativeSourceIdentity=c["source_identity"]))
    result=dict(status="PASS" if not failures else "FAIL",faces=sum(v["faces"] for v in rows),
        rows=rows,bitMismatchFailures=failures,wrongForceSignNegativeControls=counterexamples,
        probeRecordSha256=hashlib.sha256(a.probe_record.read_bytes()).hexdigest(),
        limitations=["Binary64 anchored Neumaier reconstruction checks export mapping only",
            "Exact Fraction stencil delta uses stored potential/coefficient/boundary values",
            "No ideal coefficient error, continuous force, spatial acceptance, Runtime or CUDA grant"])
    a.output.parent.mkdir(parents=True,exist_ok=True)
    a.output.write_text(json.dumps(result,indent=2)+"\n")
    print("RZ_FACE_GRADIENT_MAPPING",result["status"],result["faces"],"wrong-sign controls",counterexamples)
    if failures:raise SystemExit(1)
if __name__=="__main__":main()
