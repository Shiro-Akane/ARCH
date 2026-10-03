"""Run the existing frozen CPU Sod endpoint paired with read-only Plotfile traffic."""
import argparse,hashlib,json,re,subprocess,statistics
from pathlib import Path
import h5py
import numpy as np

def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()
def values(t):
    out={}
    for line in t.splitlines():
        line=line.split("#",1)[0].strip()
        if not line: continue
        k,sep,v=line.partition("=")
        assert sep and k.strip() not in out
        out[k.strip()]=v.strip()
    return out
def equal_datasets(a,b,group=None):
    rows=[]
    with h5py.File(a) as x,h5py.File(b) as y:
        target=x[group] if group else x
        def visit(n,d):
            if not isinstance(d,h5py.Dataset):return
            path=f"{group}/{n}" if group else n
            other=y[path]
            assert d.shape==other.shape and d.dtype==other.dtype,path
            av,bv=d[()],other[()]
            assert (av.tobytes()==bv.tobytes() if d.dtype.kind in "fiu" else np.array_equal(av,bv)),path
            rows.append(path)
        target.visititems(visit)
    return rows
p=argparse.ArgumentParser(description=__doc__)
p.add_argument("--project",type=Path,required=True)
p.add_argument("--node",type=Path,required=True)
p.add_argument("--output-root",type=Path,required=True)
a=p.parse_args();r=a.project.resolve();out=a.output_root.resolve()
out.mkdir(parents=True,exist_ok=False)
frozen=r/"studio/.local/runs/795e6be7-105e-4b3d-a48a-eae68a6f4303/input.par"
frozen_hash="381ceb60eb6cdbf7c99b43577ff0d94731144d655514c7a8194d09ce8d88644e"
assert sha(frozen)==frozen_hash
text=frozen.read_text();base=values(text)
assert base["tmax"]=="0.2" and base["compute_backend"]=="cpu" and base["restart"]=="false"
old_final=Path(base["out_dir"])/"Sod_chk_0004.h5"
assert sha(old_final)=="13d54851cf2758601082ef3b7074cd7291c554aa168c33e7482ddc8ae4cb268d"
reader=r/"studio/.local/integration/rz-plot-cpu-integration-20261004/t0/CellularDet-0/output/reference_HLLC_plt_0000.h5"
reader_hash="6e94d795f07cca3d735e2e5dd6c1129b4efe6478af811fb95ffbcc2066fad755"
assert sha(reader)==reader_hash
plan={"binary":str(r/"build-cpu/bin/ARCH"),"readerFile":str(reader),"runs":[]}
for i in range(6):
    work=out/f"run-{i}";work.mkdir()
    modified=re.sub(r"^out_dir\s*=.*$","out_dir = "+str(work/"output"),text,flags=re.M)
    assert {k:v for k,v in values(modified).items() if k!="out_dir"}=={k:v for k,v in base.items() if k!="out_dir"}
    config=work/"Sod.par";config.write_text(modified)
    plan["runs"].append({"concurrent":i%2==1,"config":str(config),"stdout":str(work/"stdout.log"),"stderr":str(work/"stderr.log")})
plan_file=out/"plan.json";plan_file.write_text(json.dumps(plan,indent=2))
q=subprocess.run([str(a.node),str(r/"validation/io/measure_plotfile_run_interference.mjs"),str(r),str(plan_file)],capture_output=True,text=True,timeout=180)
(out/"measurement.stdout").write_text(q.stdout);(out/"measurement.stderr").write_text(q.stderr)
if q.returncode:print(q.stderr);raise SystemExit(q.returncode)
result=json.loads(q.stdout)
reference_plot=None
for run,row in zip(plan["runs"],result["rows"]):
    output=Path(run["config"]).parent/"output";chk=output/"Sod_chk_0004.h5"
    with h5py.File(chk) as h:
        assert float(h.attrs["time"])==.2 and int(h.attrs["step"])==280
    row["checkpointDatasetCount"]=len(equal_datasets(chk,old_final))
    plot=output/"Sod_plt_0004.h5"
    if reference_plot is None:reference_plot=plot
    row["fieldDatasetCount"]=len(equal_datasets(plot,reference_plot,"Data"))
    with h5py.File(plot) as h:
        attrs=h["SourceIdentity"].attrs
        assert attrs["binary_sha256"]==result["binarySha256"]
        assert attrs["raw_config_sha256"]==sha(Path(run["config"]))
        assert attrs["case_id"]=="Sod"
    row["finalTime"]=.2;row["finalStep"]=280;row["rawBitsEqual"]=True
    if row["concurrent"]:assert sum(x["overlapMs"] for x in row["readerQueries"])>0
baseline=[x["elapsedMs"] for x in result["rows"] if not x["concurrent"]]
concurrent=[x["elapsedMs"] for x in result["rows"] if x["concurrent"]]
result["timing"]={"baselineMs":baseline,"concurrentMs":concurrent,
 "baselineMedianMs":statistics.median(baseline),"concurrentMedianMs":statistics.median(concurrent),
 "medianRatio":statistics.median(concurrent)/statistics.median(baseline)}
result["status"]="PASS"
result["sourceHead"]=subprocess.check_output(["git","-C",str(r),"rev-parse","HEAD"],text=True).strip()
result["frozenInputSha256"]=frozen_hash
result["rawLocalDirectory"]=str(out)
assert sha(frozen)==frozen_hash and sha(reader)==reader_hash
(out/"summary.json").write_text(json.dumps(result,indent=2)+"\n")
print(json.dumps({"status":result["status"],"timing":result["timing"],"rows":len(result["rows"]),"summary":str(out/"summary.json")}))
