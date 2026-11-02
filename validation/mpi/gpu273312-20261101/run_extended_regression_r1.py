from pathlib import Path
import json,os,importlib.util,hashlib,datetime,subprocess,re,shutil,traceback
R=Path("/home/ubuntu/projects/ARCH-mpi")
E=Path("/home/ubuntu/projects/ARCH-mpi-evidence-gpu273312-20261101")
T=Path("/home/ubuntu/projects/ARCH-mpi-toolchain/env")
os.environ.update(PATH=str(T/"bin")+":"+os.environ["PATH"],OMP_NUM_THREADS="1",OPENBLAS_NUM_THREADS="1",PYTHONDONTWRITEBYTECODE="1")
spec=importlib.util.spec_from_file_location("mpi_validation",R/"tests/integration/test_mpi_fixed_grid.py")
v=importlib.util.module_from_spec(spec);spec.loader.exec_module(v)
status=json.loads((E/"execution-status-r1.json").read_text())
assert status["status"]=="short_regression_passed"
cross=[]
old=Path("/home/ubuntu/projects/ARCH-mpi-evidence/application-attempt04")
for directory in sorted(old.glob("*/*/output")):
 if not list(directory.glob("*_plt_*.h5")) and not list(directory.glob("rank-*")):continue
 rel=directory.relative_to(old)
 cross.append({"lane":str(rel),**v.compare(v.snapshots(directory),v.snapshots(E/"debug-application"/rel))})
assert len(cross)==32,len(cross)
(E/"cross-host-debug.json").write_text(json.dumps({"source":"gen-989544","destination":"gpu-273312","comparisons":cross},indent=2))
print("PASS cross-host",len(cross),"lanes",flush=True)
out=E/"release-256-step"
out.mkdir(exist_ok=False)
report={"source_commit":"29972a167c9bc0bb77bdb07557f920f54bfad64f","steps":256,"roots":17,"global_pool_capacity":18,"cases":[],"started_utc":datetime.datetime.now(datetime.timezone.utc).isoformat(),"status":"running","no_oversubscription":True}
bins={k:Path(p)/"bin/ARCH" for k,p in status["variants"]["release"]["build_dirs"].items()}
report["binary_sha256"]={k:hashlib.sha256(p.read_bytes()).hexdigest() for k,p in bins.items()}
def save(): (out/"summary.json").write_text(json.dumps(report,indent=2))
try:
 for problem,integrator,solver,boundary in [("Sod","Euler","HLLC",None),("Sod","RK2","HLLC",None),("Sod","RK3","HLL",None),("SmoothAdvection","RK2","HLLC",None),("SmoothAdvection","RK3","Roe",None),("Sod","RK2","HLLC","reflecting")]:
  name=f"{problem}-{integrator}-{solver}"+("-"+boundary if boundary else "")
  base=(E/"release-application"/name/"baseline/run.par").read_text()
  source=v.parameters(base,{"nblockx1":17,"max_blocks":18,"max_steps":256,"tmax":1.0,"plt_dstep":16})
  reference=None
  for ranks in [0,1,2,4,8,16]:
   assert shutil.disk_usage(E).free>24*2**30
   lane=out/name/("baseline" if ranks==0 else f"mpi-{ranks}");lane.mkdir(parents=True)
   par=lane/"run.par";par.write_text(v.parameters(source,{"out_dir":lane/"output"}))
   v.run(bins["mpi"] if ranks else bins["baseline"],problem,par,lane/"run.log",ranks,["mpiexec"])
   log=(lane/"run.log").read_text()
   completed=[int(x) for x in re.findall(r"Simulation Done\. Total Steps:\s*(\d+)",log)]
   assert len(completed)==max(1,ranks) and set(completed)=={256},(name,ranks,completed)
   state=v.snapshots(lane/"output")
   assert len(state)==17,len(state)
   if not ranks:reference=state;continue
   capacities=[]
   if ranks>1:
    assert not list((lane/"output").rglob("*_chk_*.h5"))
    for rank in range(ranks):
     meta=dict(l.split("=",1) for l in (lane/"output"/f"rank-{rank}"/"partition.txt").read_text().splitlines())
     assert int(meta["root_begin"])==17*rank//ranks
     assert int(meta["root_end"])==17*(rank+1)//ranks
     own=int(meta["root_end"])-int(meta["root_begin"])
     capacity=int(meta["local_pool_capacity"])
     assert capacity>=own>0
     capacities.append(capacity)
    assert sum(capacities)==18
   result=v.compare(reference,state)
   report["cases"].append({"case":name,"ranks":ranks,"completed_steps":256,"pool_capacities":capacities,**result});save()
   print("PASS extended",name,"ranks",ranks,"256 steps",flush=True)
 assert len(report["cases"])==30
 report.update(status="passed",finished_utc=datetime.datetime.now(datetime.timezone.utc).isoformat());save()
 print("ALL 30 EXTENDED COMPARISONS PASSED",flush=True)
except BaseException as ex:
 report.update(status="failed",error=repr(ex),traceback=traceback.format_exc());save();raise
