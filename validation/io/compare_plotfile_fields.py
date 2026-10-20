"""Read-only exact cross-build field comparison with explicit run identities."""
import argparse, hashlib, json
from pathlib import Path
import h5py
import numpy as np
from run_plotfile_fields_t0 import entries, sha

def compare(current, prior):
    if current["case"] != prior["case"]:
        raise ValueError("Case mismatch")
    case = current["case"]
    directories = [Path(row["localEvidenceDirectory"]) for row in (current, prior)]
    paths = []
    for row, directory in zip((current, prior), directories):
        config = directory/(case+".par")
        if sha(config) != row["inputSha256"]:
            raise ValueError("Recorded input bytes changed")
        plots = list((directory/"output").glob("*plt*.h5"))
        if len(plots) != 1:
            raise ValueError("Require exactly one recorded Plotfile")
        paths.append(plots[0])
    values = [entries((d/(case+".par")).read_text()) for d in directories]
    changed = sorted(k for k in set(values[0]) | set(values[1])
                     if values[0].get(k) != values[1].get(k))
    if changed != ["out_dir"]:
        raise ValueError("Input differs beyond independent output directory: "+str(changed))
    digests = [sha(path) for path in paths]
    fields = {}
    with h5py.File(paths[0], "r") as a, h5py.File(paths[1], "r") as b:
        for row, h in zip((current, prior), (a, b)):
            attrs = h["SourceIdentity"].attrs
            def text(v):
                return v.decode() if isinstance(v, bytes) else str(v)
            if text(attrs["case_id"]) != case or text(attrs["raw_config_sha256"]) != row["inputSha256"] or text(attrs["binary_sha256"]) != row["binarySha256"]:
                raise ValueError("Recorded output source identity mismatch")
            if h.attrs["time"] != 0:
                raise ValueError("Only approved t=0 comparison")
        if set(a["Data"]) != set(b["Data"]):
            raise ValueError("Exported field sets differ")
        for path in ["Grid/level", "Grid/morton", *["NativeGrid/logical_x"+str(i) for i in (1,2,3)]]:
            if a[path][:].tobytes() != b[path][:].tobytes():
                raise ValueError("Block ordering differs; no silent Morton reorder")
        for name in a["Data"]:
            av, bv = a["Data/"+name][:], b["Data/"+name][:]
            if av.shape != bv.shape or av.dtype != bv.dtype or av.dtype != np.dtype("float64"):
                raise ValueError("FP64 shape mismatch: "+name)
            mismatch = int(np.count_nonzero(av.view(np.uint64) != bv.view(np.uint64)))
            fields[name] = {"shape":list(av.shape), "samples":av.size,
                "bitMismatchCount":mismatch,
                "currentRawArraySha256":hashlib.sha256(av.tobytes()).hexdigest(),
                "priorRawArraySha256":hashlib.sha256(bv.tobytes()).hexdigest()}
    if digests != [sha(path) for path in paths]:
        raise ValueError("Source changed during read")
    return {"case":case,"currentPlotfileSha256":digests[0],"priorPlotfileSha256":digests[1],
            "currentBinarySha256":current["binarySha256"],"priorBinarySha256":prior["binarySha256"],
            "currentInputSha256":current["inputSha256"],"priorInputSha256":prior["inputSha256"],
            "changedInputKeys":changed,"sameBlockOrder":True,"fields":fields,
            "allFieldBitsMatch":all(f["bitMismatchCount"]==0 for f in fields.values()),
            "sourcesUnchanged":True,"scope":"Exact stored t=0 numeric identity, not independent scientific correctness"}

if __name__ == "__main__":
    parser=argparse.ArgumentParser(description=__doc__)
    for key in ["current-runs","prior-runs","output"]:
        parser.add_argument("--"+key,required=True,type=Path)
    args=parser.parse_args()
    old={r["case"]:r for r in json.loads(args.prior_runs.read_text())}
    result=[compare(row,old[row["case"]]) for row in json.loads(args.current_runs.read_text())]
    args.output.write_text(json.dumps(result,indent=2)+"\n")
    print(json.dumps([{"case":r["case"],"fields":len(r["fields"]),"allFieldBitsMatch":r["allFieldBitsMatch"]} for r in result]))
    raise SystemExit(0 if all(r["allFieldBitsMatch"] for r in result) else 1)
