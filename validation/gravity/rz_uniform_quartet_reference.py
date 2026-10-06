#!/usr/bin/env python3
"""Independent exact union proof from original native cell metadata.
No production geometry/helper import, kernel copy, averaged density or new
threshold. Full ring's common pi factor cancels in the mass comparison.
This checks finite source partition semantics, not continuous field accuracy.
"""
from fractions import Fraction as F
from pathlib import Path
import argparse,json,hashlib
p=argparse.ArgumentParser(description=__doc__)
p.add_argument("--record",type=Path,action="append",required=True)
p.add_argument("--output",type=Path,required=True)
a=p.parse_args();records=[]
for path in a.record:
    document=json.loads(path.read_text());rows=[]
    for case in document["cases"]:
        groups={}
        for cell in case["cells"]:
            key=(cell["level"]-1,tuple(i//2 for i in cell["index"][:2]))
            groups.setdefault(key,[]).append(cell)
        eligible=0;rejected=0
        for cells in groups.values():
            if len(cells)!=4:continue
            by_octant={(c["index"][0]%2)+2*(c["index"][1]%2):c for c in cells}
            if set(by_octant)!=set(range(4)):continue
            ordered=[by_octant[i] for i in range(4)]
            density=[F(c["density"]) for c in ordered]
            if len(set(density))!=1:rejected+=1;continue
            e=[[F(x) for x in c["edges"]] for c in ordered]
            rl,rm,rh=e[0][0],e[0][1],e[3][1]
            zl,zm,zh=e[0][2],e[0][3],e[3][3]
            assert rl<rm<rh and zl<zm<zh
            for i in range(4):
                assert e[i]==[rm if i&1 else rl,rh if i&1 else rm,
                              zm if i&2 else zl,zh if i&2 else zm]
            # Exact rectangular disjoint union proves additivity for the
            # existing integrable complete-ring Newton kernel. Polynomial
            # volume/first moments are additional scalar consistency checks.
            for radial_power in (1,2,3):
                for axial_power in (0,1,2):
                    def integral(edges):
                        lo,hi,bottom,top=edges
                        return (hi**(radial_power+1)-lo**(radial_power+1))/(radial_power+1)*(
                            top**(axial_power+1)-bottom**(axial_power+1))/(axial_power+1)
                    assert sum((density[i]*integral(e[i]) for i in range(4)),F(0))==density[0]*integral([rl,rh,zl,zh])
            eligible+=1
        assert eligible>0
        accepted=case["coalesced_acceptances"]
        assert accepted>0 and case["coalesced_native_leaves"]==4*accepted
        assert case["coalesced_attempts"]>=accepted
        rows.append({"cells":len(case["cells"]),"radialOrigin":case["radial_origin"],
                     "exactEligibleQuartets":eligible,"unequalDensityGroups":rejected,
                     "acceptedObserverIntegrals":accepted,
                     "representedOriginalLeaves":case["coalesced_native_leaves"],
                     "exactUnionAndMoments":"PASS"})
    records.append({"recordSha256":hashlib.sha256(path.read_bytes()).hexdigest(),"rows":rows})
a.output.write_text(json.dumps({"status":"EXACT_NATIVE_QUARTET_UNION_PASS","records":records,
    "limitations":["Original small solved-record partition semantics only, not all Runtime arrays",
                   "No continuous potential/force or evolution acceptance"]},indent=2)+"\n")
print("RZ_UNIFORM_QUARTET_FRACTION_PASS",sum(len(x["rows"]) for x in records))
