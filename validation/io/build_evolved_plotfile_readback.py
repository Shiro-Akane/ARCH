"""Create a local independent h5py point oracle for the existing Sod continuation."""
import argparse, hashlib, json, struct
from pathlib import Path
import h5py

def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()
def bits(v): return struct.pack("<d",float(v)).hex()

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--evidence-root",required=True)
    ap.add_argument("--manifest",required=True)
    args=ap.parse_args()
    root=Path(args.evidence_root).resolve()
    summary=json.loads((root/"summary.json").read_text())
    assert summary["status"]=="PASS" and summary["distinctOutputSessionIds"]
    rows=[]
    for session in summary["sessions"]:
        for path in sorted((root/session["mode"]/"output").glob("*_plt_*.h5")):
            with h5py.File(path) as f:
                assert int(f.attrs["dim"])==1 and f.attrs["geometry"]=="cartesian"
                shape=f["Data/DENS"].shape;assert shape==(8,16)
                src=f["SourceIdentity"].attrs
                assert src["run_id"]==session["runId"]
                assert src["raw_config_sha256"]==session["inputSha256"]
                assert src["binary_sha256"]==summary["binarySha256"]
                points=[]
                for index in (8,63,64,119):
                    b,i=divmod(index,shape[1])
                    points.append(dict(index=index,block=b,start=[i],
                        point=[float(f["Grid/x"][index])],
                        coordinates={axis:bits(f["Grid/"+axis][index]) for axis in ("x","y","z")},
                        lower={axis:bits(f["NativeGrid/"+axis+"_lower"][index]) for axis in ("x1","x2","x3")},
                        upper={axis:bits(f["NativeGrid/"+axis+"_upper"][index]) for axis in ("x1","x2","x3")},
                        measure=bits(f["NativeGrid/cell_measure"][index]),
                        level=int(f["Grid/level"][b]),
                        logicalKey="/".join(str(v) for v in [int(f["Grid/level"][b])]+[int(f["NativeGrid/logical_"+a][b]) for a in ("x1","x2","x3")]),
                        fields={name:dict(bits=bits(f["Data/"+name][b,i]),unit=str(f["Data/"+name].attrs["unit"]))
                            for name in f["Data"]}))
                rows.append(dict(path=str(path),sha256=sha(path),time=float(f.attrs["time"]),
                    mode=session["mode"],runId=session["runId"],inputSha256=session["inputSha256"],
                    binarySha256=summary["binarySha256"],cells=128,points=points))
    dest=Path(args.manifest);assert not dest.exists()
    dest.write_text(json.dumps(rows,indent=2)+"\n")
    print(json.dumps(dict(files=len(rows),points=sum(len(x["points"]) for x in rows),
        fieldPoints=sum(len(p["fields"]) for x in rows for p in x["points"]),
        localManifest=str(dest)),indent=2))

if __name__=="__main__": main()
