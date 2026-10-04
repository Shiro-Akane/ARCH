"""Independent matched piecewise-constant full-ring reference composition.
Axis analytic; off-axis exterior only. Decimal estimates, no certified bound,
production kernel, inferred density, spatial acceptance or simulation.
"""
import argparse
import hashlib
import json
from decimal import Decimal, localcontext
from pathlib import Path
from rz_ring_axis_reference import G, axis_reference, decimal_value
from rz_ring_offaxis_reference import finite_volume_reference, contact_potential_reference, decimal_gauss

KEYS = ("r_lower", "r_upper", "z_lower", "z_upper", "density")

def validate_source(source):
    if not isinstance(source.get("sourceId"), str) or not source["sourceId"]:
        raise ValueError("Explicit sourceId required; density may not be inferred")
    leaves = source.get("leaves")
    if not isinstance(leaves, list) or not leaves:
        raise ValueError("Explicit nonempty leaf source required")
    ids = set()
    parsed = []
    for leaf in leaves:
        uid = leaf.get("id")
        if not isinstance(uid, str) or not uid or uid in ids:
            raise ValueError("Unique explicit leaf id required")
        ids.add(uid)
        values = tuple(decimal_value(leaf[k]) for k in KEYS)
        if not all(x.is_finite() for x in values):
            raise ValueError("Finite source required")
        rl, rr, zl, zr, rho = values
        if not (0 <= rl < rr and zl < zr and rho >= 0):
            raise ValueError("Ordered geometry and nonnegative density required")
        parsed.append(dict(zip(KEYS, values), id=uid))
    # A native leaf partition may touch but cannot double-count volume.
    for i, a in enumerate(parsed):
        for b in parsed[:i]:
            if (max(a["r_lower"],b["r_lower"]) < min(a["r_upper"],b["r_upper"])
                and max(a["z_lower"],b["z_lower"]) < min(a["z_upper"],b["z_upper"])):
                raise ValueError("Overlapping leaf source")
    return parsed

def reference(source, observer, *, order=32, precision=80):
    # Validate settings even for axis / zero density.
    decimal_gauss(order, precision)
    leaves = validate_source(source)
    R, Z = (decimal_value(observer[k]) for k in ("r_observer","z_observer"))
    if not (R.is_finite() and Z.is_finite() and R >= 0):
        raise ValueError("Finite observer with nonnegative radius required")
    with localcontext() as ctx:
        ctx.prec = precision
        total = dict.fromkeys(("potential","radial_acceleration","axial_acceleration","mass"),Decimal(0))
        for leaf in leaves:
            if leaf["density"] == 0:
                continue  # exact zero source, not discarded negative density
            if R == 0:
                values = axis_reference(*(leaf[k] for k in KEYS), Z, precision,
                                        decimal_output=True)
            else:
                case = dict(leaf, r_observer=R, z_observer=Z)
                # Any unsupported contributing leaf rejects the whole observer.
                values = finite_volume_reference(case, order, precision)
            total = {k:total[k]+values[k] for k in total}
        return total

def potential_reference(source, observer, *, order=32, precision=80, t_panels=1):
    """Separate approved contact Phi diagnostic; never supplies contact force."""
    decimal_gauss(order, precision)
    if type(t_panels) is not int or not 1 <= t_panels <= 4:
        raise ValueError("Contact diagnostic t_panels must be 1..4")
    leaves=validate_source(source)
    R,Z=(decimal_value(observer[k]) for k in ("r_observer","z_observer"))
    if not (R.is_finite() and Z.is_finite() and R>=0):
        raise ValueError("Invalid observer")
    with localcontext() as ctx:
        ctx.prec=precision
        total=Decimal(0);contact=exterior=axis=0
        for leaf in leaves:
            if leaf["density"]==0:continue
            case=dict(leaf,r_observer=R,z_observer=Z)
            if R==0:
                value=axis_reference(*(leaf[k] for k in KEYS),Z,precision,
                                     decimal_output=True)["potential"]
                axis+=1
            elif (leaf["r_lower"]<=R<=leaf["r_upper"]
                  and leaf["z_lower"]<=Z<=leaf["z_upper"]):
                value=contact_potential_reference(case,order,precision,t_panels)["potential"]
                contact+=1
            else:
                value=finite_volume_reference(case,order,precision)["potential"]
                exterior+=1
            total+=value
        return dict(potential=total,contactLeaves=contact,exteriorLeaves=exterior,
                    axisLeaves=axis,certified=False)

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--input",required=True,type=Path)
    p.add_argument("--output",required=True,type=Path)
    p.add_argument("--order",type=int,default=32)
    p.add_argument("--precision",type=int,default=80)
    a=p.parse_args()
    if a.output.exists():p.error("output must be new")
    data=json.loads(a.input.read_text())
    validate_source(data)
    rows=[]
    for observer in data["observers"]:
        try:
            result=reference(data,observer,order=a.order,precision=a.precision)
            row={"id":observer["id"],"status":"ESTIMATE",
                 "valuesDecimal":{k:str(v) for k,v in result.items()}}
        except (ValueError,ArithmeticError) as error:
            row={"id":observer["id"],"status":"UNSUPPORTED_OR_FAILED","reason":str(error)}
        rows.append(row)
    out={"sourceId":data["sourceId"],"inputSha256":hashlib.sha256(a.input.read_bytes()).hexdigest(),
         "leafCount":len(data["leaves"]),"order":a.order,"precision":a.precision,
         "GDecimal":str(G),"units":{"potential":"cm^2/s^2","acceleration":"cm/s^2","mass":"g"},
         "rows":rows,"scope":"Full-ring matched explicit leaf density; axis analytic / off-axis exterior estimates only",
         "limitations":["No certified quadrature error or continuous science PASS",
                        "No off-axis interior/contact force, CUDA, evolution or Runtime source publication proof"]}
    a.output.parent.mkdir(parents=True,exist_ok=True)
    a.output.write_text(json.dumps(out,indent=2)+"\n")
    print("MATCHED_SOURCE_REFERENCE",len(rows))
if __name__=="__main__":main()
