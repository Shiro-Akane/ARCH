"""Verify run UUIDs on approved CPU t=0 inputs; keep all raw output local."""
import argparse, hashlib, json, os, re, shutil, subprocess, uuid
from pathlib import Path
import h5py
import numpy as np

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary",required=True)
    parser.add_argument("--references",required=True)
    parser.add_argument("--output-root",required=True)
    parser.add_argument("--attempts",type=int,choices=(1,2),default=2,
        help="Use one run per case for cross-build regression; two for repeat UUID evidence")
    args=parser.parse_args()
    binary=Path(args.binary).resolve(); digest=sha(binary)
    root=Path(args.output_root).resolve();root.mkdir(parents=True,exist_ok=False)
    rows=[]
    for directory in json.loads(Path(args.references).read_text()):
        ref=Path(directory); record=json.loads((ref/"summary.json").read_text())
        case=record["case"]; assert case in ("Sod","CellularDet")
        config=ref/(case+".par");text=config.read_text()
        assert sha(config)==record["inputSha256"]
        values={}
        for line in text.splitlines():
            line=line.split("#",1)[0].strip()
            if not line:continue
            key,sep,value=line.partition("=")
            assert sep and key.strip() not in values
            values[key.strip()]=value.strip()
        assert values["tmax"]=="0" and values["max_steps"]=="-1" and values["compute_backend"]=="cpu"
        old_plot=next((ref/"output").glob("*_plt_*.h5"))
        old_chk=next((ref/"output").glob("*_chk_*.h5"))
        for attempt in range(args.attempts):
            evidence=root/(case+"-"+str(attempt));evidence.mkdir()
            output=evidence/"output"
            updated=re.sub(r"^out_dir\s*=.*$","out_dir="+str(output),text,flags=re.MULTILINE)
            assert updated!=text
            par=evidence/(case+".par");par.write_text(updated)
            with (evidence/"stdout.log").open("w") as stdout,(evidence/"stderr.log").open("w") as stderr:
                result=subprocess.run([str(binary),case,str(par)],cwd=evidence,
                    env=dict(os.environ,OMP_NUM_THREADS="1",CUDA_VISIBLE_DEVICES=""),
                    stdout=stdout,stderr=stderr,timeout=90)
            assert result.returncode==0 and sha(binary)==digest
            plot=next(output.glob("*_plt_*.h5"));chk=next(output.glob("*_chk_*.h5"))
            with h5py.File(plot) as f,h5py.File(old_plot) as old:
                attrs=f["SourceIdentity"].attrs
                run=str(attrs["run_id"]); parsed=uuid.UUID(run)
                assert parsed.version==4 and str(parsed)==run
                assert attrs["run_id_source"]=="DriverIO output session; OS-generated UUIDv4"
                assert attrs["binary_sha256"]==digest and attrs["raw_config_sha256"]==sha(par)
                assert f.attrs["time"]==0
                assert set(f["Data"])==set(old["Data"])
                for name in f["Data"]:
                    assert f["Data/"+name].dtype==old["Data/"+name].dtype
                    assert np.array_equal(f["Data/"+name][()].view(np.uint64),old["Data/"+name][()].view(np.uint64))
                field_count=len(f["Data"])
            datasets=[]
            with h5py.File(chk) as f,h5py.File(old_chk) as old:
                assert float(f.attrs["time"])==0.0 and int(f.attrs["step"])==0
                if "geometry_semantics_revision" in f.attrs:
                    assert int(f.attrs["geometry_semantics_revision"])==1
                    assert f.attrs["geometry_chart"]=="existing"
                def compare(name,obj):
                    if isinstance(obj,h5py.Dataset) and obj.dtype.kind in "fiu":
                        assert name in old and obj.shape==old[name].shape and obj.dtype==old[name].dtype
                        assert obj[()].tobytes()==old[name][()].tobytes(),name
                        datasets.append(name)
                f.visititems(compare)
            rows.append(dict(case=case,runId=run,plotfile=str(plot),plotfileSha256=sha(plot),
                inputSha256=sha(par),binarySha256=digest,fieldCount=field_count,
                checkpointNumericDatasets=len(datasets),fieldAndCheckpointBitsUnchanged=True))
    assert len({row["runId"] for row in rows})==len(rows)
    (root/"summary.json").write_text(json.dumps(rows,indent=2)+"\n")
    print(json.dumps(rows,indent=2))

if __name__=="__main__":
    main()
