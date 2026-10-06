"""Read-only consolidation of completed Core-frozen uniform JENS lanes.

Uses original local HDF5 files; never launches ARCH or modifies raw data.
CLI: check_jeans_uniform.py first-result.json corrected-3d-result.json
"""
import csv
import json
import sys
from pathlib import Path
import h5py
import numpy as np


def consolidate(paths):
    cases = {}
    identities = set()
    for path in paths:
        result = json.loads(Path(path).read_text())
        identities.add(result["binarySha256"])
        for case in result["cases"]:
            if case.get("restart_pass"):
                cases[(case["dimension"], case["lane"])] = case
    assert len(identities) == 1, "mixed executable identities"
    assert set(cases) == {(d,l) for d in (1,2,3)
                          for l in ("disabled","output_only","active")}
    rows = []
    for (dim,lane), case in sorted(cases.items()):
        folder = Path(case["config"]["out_dir"])
        plots = sorted(folder.glob("*plt*.h5"))
        assert len(plots) == 3
        for path in plots:
            with h5py.File(path) as h:
                for key,value in {"DENS":1e7,"TEMP":1,"ENER":1e7,
                                  "PRES":(1.6666666666666667-1)*1e7,"gas":1}.items():
                    assert np.all(h["Data/"+key][:] == value), (path,key)
                for key in (*("VELX","VELY","VELZ")[:dim],"GPOT",*("GACX","GACY","GACZ")[:dim]):
                    assert np.all(h["Data/"+key][:] == 0), (path,key)
        with h5py.File(max(folder.glob("*chk*.h5"))) as h:
            final_step = int(h.attrs["step"])
            assert float(h.attrs["time"]) == .02
        accepted_coverage = None
        if lane == "active":
            with (folder/"GravityBox_regrid.tsv").open() as f:
                recorded = {int(x["macro_step"]) for x in csv.DictReader(f,delimiter="\t")}
            assert set(range(final_step+1)) <= recorded, "missing accepted-state transaction"
            accepted_coverage = final_step+1
        rows.append({k:case[k] for k in
                     ("name","dimension","lane","cells","elapsed_seconds",
                      "restart_elapsed_seconds","mass_relative_drift",
                      "energy_minus_nuclear_over_initial_gas","solves","stage_solves")} |
                    {"restartExact":True,"uniformFieldsExact":True,
                     "acceptedStateTransactionSteps":accepted_coverage,
                     "maxJensRelativeError":max((x["maxRelativeError"] for x in case["jens_checks"]),default=None),
                     "rawDirectory":str(folder)})
    comparisons = []
    for dim in (1,2,3):
        off, output = (cases[(dim,l)] for l in ("disabled","output_only"))
        assert all(off[k]==output[k] for k in ("cells","solves","stage_solves","max_iterations"))
        offdir,outdir = (Path(c["config"]["out_dir"]) for c in (off,output))
        for a,b in zip(sorted(offdir.glob("*plt*.h5")),sorted(outdir.glob("*plt*.h5")),strict=True):
            with h5py.File(a) as h,h5py.File(b) as g:
                assert h.attrs["time"] == g.attrs["time"]
                for group in ("Data","Grid"):
                    for key in h[group]:
                        assert np.array_equal(h[group][key][:],g[group][key][:]),(dim,group,key)
        for a,b in zip(sorted(offdir.glob("*chk*.h5")),sorted(outdir.glob("*chk*.h5")),strict=True):
            with h5py.File(a) as h,h5py.File(b) as g:
                assert set(h)==set(g)
                assert set(h.attrs)==set(g.attrs)
                assert all(np.array_equal(h.attrs[k],g.attrs[k]) for k in h.attrs)
                for key in h:
                    if isinstance(h[key],h5py.Dataset):
                        assert np.array_equal(h[key][:],g[key][:]),(dim,key)
        comparisons.append({"dimension":dim,"offVsOutputNativeStateExact":True,
                            "offVsOutputSolveCountsExact":True})
    return {"contract":"uniform-lifecycle-1","status":"PASS",
            "scope":"uniform Cartesian single-caloric-species CPU short evolution; not complete JENS/RZ/CUDA acceptance",
            "binarySha256":identities.pop(),"cases":rows,"comparisons":comparisons}


if __name__ == "__main__":
    print(json.dumps(consolidate(sys.argv[1:]),indent=2))
