"""Recheck the existing .05 -> .2 fixed-grid Sod continuation; raw outputs stay local."""
import argparse, hashlib, json, os, re, subprocess, uuid
from pathlib import Path
import h5py
import numpy as np

def sha(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()

def values(text):
    result={}
    for line in text.splitlines():
        line=line.split("#",1)[0].strip()
        if not line: continue
        k,sep,v=line.partition("=")
        assert sep and k.strip() not in result
        result[k.strip()]=v.strip()
    return result

def compare(a,b,group=None):
    rows=[]
    with h5py.File(a) as x,h5py.File(b) as y:
        def visit(name,obj):
            if not isinstance(obj,h5py.Dataset): return
            assert name in y and obj.shape==y[name].shape and obj.dtype==y[name].dtype,name
            va,vb=obj[()],y[name][()]
            if obj.dtype.kind in "fiu":
                assert va.tobytes()==vb.tobytes(),name
            else:
                assert np.array_equal(va,vb),name
            rows.append(dict(path=name,shape=list(obj.shape),dtype=str(obj.dtype),equal=True))
        if group:
            assert set(x[group])==set(y[group])
            x[group].visititems(lambda name,obj: visit(group+"/"+name,obj))
        else:
            dx=[];dy=[]
            x.visititems(lambda n,o: dx.append(n) if isinstance(o,h5py.Dataset) else None)
            y.visititems(lambda n,o: dy.append(n) if isinstance(o,h5py.Dataset) else None)
            assert dx==dy
            x.visititems(visit)
            for key in y.attrs:
                assert key in x.attrs and np.array_equal(x.attrs[key],y.attrs[key]),key
    return rows

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--project",required=True)
    ap.add_argument("--binary",required=True)
    ap.add_argument("--output-root",required=True)
    args=ap.parse_args()
    project=Path(args.project).resolve();binary=Path(args.binary).resolve()
    root=Path(args.output_root).resolve();root.mkdir(parents=True,exist_ok=False)
    digest=sha(binary)
    ids=["795e6be7-105e-4b3d-a48a-eae68a6f4303","30db4e5c-7c43-4cac-8b30-9af8fad393db"]
    hashes=["381ceb60eb6cdbf7c99b43577ff0d94731144d655514c7a8194d09ce8d88644e",
            "6f9943549d61aebc4879a150300901d7281f2c9c2bcb9bd825e01731d591647d"]
    frozen=[project/"studio/.local/runs"/i/"input.par" for i in ids]
    for p,h in zip(frozen,hashes): assert sha(p)==h
    texts=[p.read_text() for p in frozen]; parsed=[values(t) for t in texts]
    assert parsed[0]["tmax"]==parsed[1]["tmax"]=="0.2"
    assert parsed[0]["compute_backend"]==parsed[1]["compute_backend"]=="cpu"
    assert parsed[0]["lrefinemin"]==parsed[0]["lrefinemax"]=="0"
    assert parsed[0]["restart"]=="false" and parsed[1]["restart"]=="true"
    ignored={"out_dir","restart","restart_file"}
    assert {k:v for k,v in parsed[0].items() if k not in ignored}=={k:v for k,v in parsed[1].items() if k not in ignored}
    old_final=Path(parsed[0]["out_dir"])/"Sod_chk_0004.h5"
    assert sha(old_final)=="13d54851cf2758601082ef3b7074cd7291c554aa168c33e7482ddc8ae4cb268d"
    sessions=[];split=None;split_hash=None
    for index,mode in enumerate(("continuous","restart")):
        work=root/mode;work.mkdir();output=work/"output"
        text=re.sub(r"^out_dir\s*=.*$","out_dir = "+str(output),texts[index],flags=re.M)
        if index:
            assert split is not None
            text=re.sub(r"^restart_file\s*=.*$","restart_file = "+str(split),text,flags=re.M)
        actual=values(text)
        assert {k:v for k,v in actual.items() if k not in {"out_dir","restart_file"}}=={k:v for k,v in parsed[index].items() if k not in {"out_dir","restart_file"}}
        par=work/"Sod.par";par.write_text(text)
        with (work/"stdout.log").open("w") as out,(work/"stderr.log").open("w") as err:
            run=subprocess.run([str(binary),"Sod",str(par)],cwd=work,
                env=dict(os.environ,OMP_NUM_THREADS="1",CUDA_VISIBLE_DEVICES=""),
                stdout=out,stderr=err,timeout=90)
        assert run.returncode==0 and sha(binary)==digest
        plots=sorted(output.glob("*_plt_*.h5"));assert plots
        run_ids=set()
        for plot in plots:
            with h5py.File(plot) as f:
                identity=f["SourceIdentity"].attrs
                rid=str(identity["run_id"]);assert uuid.UUID(rid).version==4
                assert identity["binary_sha256"]==digest
                assert identity["raw_config_sha256"]==sha(par)
                assert identity["case_id"]=="Sod"
                run_ids.add(rid)
        assert len(run_ids)==1
        final=output/"Sod_chk_0004.h5"
        with h5py.File(final) as f:
            assert float(f.attrs["time"])==.2 and int(f.attrs["step"])==280
            assert int(f.attrs["geometry_semantics_revision"])==1 and f.attrs["geometry_chart"]=="existing"
        sessions.append(dict(mode=mode,runId=next(iter(run_ids)),inputSha256=sha(par),
            plotCount=len(plots),finalCheckpointSha256=sha(final),
            checkpoint=str(final),finalPlot=str(plots[-1])))
        if not index:
            split=output/"Sod_chk_0001.h5"
            with h5py.File(split) as f:
                assert float(f.attrs["time"])==.05 and int(f.attrs["step"])==67
            split_hash=sha(split)
        else:
            assert sha(split)==split_hash
    assert sessions[0]["runId"]!=sessions[1]["runId"]
    rows=compare(Path(sessions[0]["checkpoint"]),Path(sessions[1]["checkpoint"]))
    reference=compare(Path(sessions[0]["checkpoint"]),old_final)
    fields=compare(Path(sessions[0]["finalPlot"]),Path(sessions[1]["finalPlot"]),"Data")
    for session in sessions:
        with h5py.File(session["finalPlot"]) as f: assert float(f.attrs["time"])==.2
    for p,h in zip(frozen,hashes): assert sha(p)==h
    assert sha(old_final)=="13d54851cf2758601082ef3b7074cd7291c554aa168c33e7482ddc8ae4cb268d"
    summary=dict(status="PASS",scope="Existing fixed-grid CPU Sod engineering regression; no new desktop UAT or independent science acceptance",
        binarySha256=digest,binarySize=binary.stat().st_size,threads=1,
        sourceHead=subprocess.check_output(["git","-C",str(project),"rev-parse","HEAD"],text=True).strip(),
        frozenInputSha256=hashes,split=dict(time=.05,step=67,sha256=split_hash),
        final=dict(time=.2,step=280),sessions=sessions,
        checkpointComparisons=rows,oldReferenceDatasetCount=len(reference),fieldComparisons=fields,
        restartInputCheckpointUnchanged=True,frozenReferenceInputsAndCheckpointUnchanged=True,
        distinctOutputSessionIds=True,
        exclusions=["Cartesian 2D evolved AMR","independent pressure/temperature oracle","large-file cost",
                    "CUDA","RZ","long O9 endpoint","publication ENOSPC/power-loss"])
    (root/"summary.json").write_text(json.dumps(summary,indent=2)+"\n")
    print(json.dumps(summary,indent=2))

if __name__=="__main__": main()
