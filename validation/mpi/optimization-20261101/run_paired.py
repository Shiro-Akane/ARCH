"""Paired before/after trials on the otherwise idle four-vCPU server."""
import os,sys,json,statistics,subprocess
from pathlib import Path
E=Path("/home/ubuntu/projects/ARCH-mpi-evidence-optimization-20261101")
os.environ["ARCH_MPI_EVIDENCE"]=str(E)
sys.path.insert(0,str(E/"tools"))
from measure import *
assert socket.gethostname()=="gen-989544"
report={"host":socket.gethostname(),"microbenchmarks":[],"cases":[],"complete":False}
for ranks in (1,2,4):
 for version in ("before","after"):
  cmd=[str(T/"bin/mpiexec"),"-n",str(ranks),"--map-by","pe-list="+",".join(map(str,range(ranks)))+":ordered","--bind-to","core",str(E/"bin"/("reduction-"+version)),"20000"]
  r=subprocess.run(cmd,env=ENV,stdin=subprocess.DEVNULL,capture_output=True,text=True,timeout=120)
  (E/f"cpu-reduction-{version}-p{ranks}.log").write_text(r.stdout+r.stderr)
  assert r.returncode==0,r.stderr
  report["microbenchmarks"].append({"ranks":ranks,"version":version,"samples":[json.loads(l) for l in r.stdout.splitlines() if l.startswith("{")]})
for name,shape,steps in (("smooth2d",(16,8,0),64),("smooth3d",(8,2,2),32)):
 root=E/"paired"/name;root.mkdir(parents=True,exist_ok=False)
 baseline=root/"baseline";launch(baseline,shape,steps,0,baseline=True)
 reference=comparison.snapshots(baseline/"output")
 case={"name":name,"shape":shape,"steps":steps,"samples":[]};report["cases"].append(case)
 for repeat in range(6):
  ranks_order=(2,4) if repeat%2==0 else (4,2)
  versions=("before","after") if repeat%2==0 else ("after","before")
  for ranks in ranks_order:
   for version in versions:
    lane=root/f"repeat{repeat}-p{ranks}-{version}"
    launch(lane,shape,steps,ranks,executable=E/"bin"/("ARCH" if version=="before" else "ARCH-after"))
    case["samples"].append({"repeat":repeat,"phase":"warmup" if repeat==0 else "measured","ranks":ranks,"version":version,
                           "comparison":check(reference,lane),"metrics":metrics(lane)})
    (E/"cpu-paired-summary.json").write_text(json.dumps(report,indent=2))
    print(name,repeat,ranks,version,"passed",flush=True)
 for ranks in (2,4):
  lane=root/f"profile-p{ranks}-after"
  launch(lane,shape,steps,ranks,True,executable=E/"bin/ARCH-after")
  m=metrics(lane)
  assert all(x["mpi_profile"]["operations"]["Allgather"]["calls"]==steps for x in m["ranks"])
  case["samples"].append({"phase":"profile","ranks":ranks,"version":"after","comparison":check(reference,lane),"metrics":m})
 case["statistics"]={}
 for ranks in (2,4):
  case["statistics"][str(ranks)]={}
  for version in ("before","after"):
   samples=[s["metrics"]["critical_driver_nonoutput_seconds"] for s in case["samples"] if s["phase"]=="measured" and s["ranks"]==ranks and s["version"]==version]
   case["statistics"][str(ranks)][version]={"median":statistics.median(samples),"min":min(samples),"max":max(samples),"n":len(samples)}
 (E/"cpu-paired-summary.json").write_text(json.dumps(report,indent=2))
report["complete"]=True
(E/"cpu-paired-summary.json").write_text(json.dumps(report,indent=2))
print("COMPLETE",flush=True)
