"""Expanded single-host scaling on a fixed, recorded subset of 16 vCPUs."""
import os,sys,json,statistics,subprocess,time,hashlib,tarfile,shlex
from pathlib import Path
STORE=Path("/home/ubuntu/projects/ARCH-mpi-evidence-optimization-20261101")
E=Path("/dev/shm/arch-mpi-expanded-20261101")
os.environ["ARCH_MPI_EVIDENCE"]=str(E)
sys.path.insert(0,str(E/"tools"))
from measure import *
CORES=list(range(10))+[16,17,18,20,22,23]
report={"host":socket.gethostname(),"allocated_cores":CORES,"shared_host":True,"output_medium":"server tmpfs; raw files archived losslessly on CPU server after every run","cases":[],"microbenchmarks":[],"complete":False}
archives=[]
def archive_lane(lane):
 name="-".join(lane.relative_to(E).parts)+".tar.gz"
 target=E/"archives"/name
 files={}
 for path in sorted(lane.rglob("*")):
  if path.is_file():
   files[str(path.relative_to(E))]={"bytes":path.stat().st_size,"sha256":hashlib.sha256(path.read_bytes()).hexdigest()}
 with tarfile.open(target,"w:gz",compresslevel=6) as archive:
  archive.add(lane,arcname=str(lane.relative_to(E)))
 total=sum(x["archive_bytes"] for x in archives)+target.stat().st_size
 if total>200*2**20:raise RuntimeError("compressed evidence budget exceeded; raw data retained in server RAM")
 control=Path("/home/ubuntu/preservation/arch-mpi-optimization-20261101")
 ssh=["ssh","-i",str(control/"temporary_expanded_key"),"-o","IdentitiesOnly=yes","-o","BatchMode=yes",
      "-o","StrictHostKeyChecking=yes","-o","UserKnownHostsFile="+str(control/"known_hosts")]
 transfer_env=os.environ.copy();transfer_env.pop("LD_LIBRARY_PATH",None)
 subprocess.run(["rsync","-a","-e",shlex.join(ssh),str(target),"ubuntu@150.65.181.189:"+str(STORE/"expanded-archives")+"/"],
                env=transfer_env,stdin=subprocess.DEVNULL,check=True,timeout=60)
 archives.append({"archive":name,"archive_bytes":target.stat().st_size,"archive_sha256":hashlib.sha256(target.read_bytes()).hexdigest(),"files":files})
 (STORE/"expanded-archive-index.json").write_text(json.dumps({"host":"gen-989544","directory":str(STORE/"expanded-archives"),"archives":archives},indent=2))
def mpi(ranks):
 return [str(T/"bin/mpiexec"),"--map-by","pe-list="+",".join(map(str,CORES[:ranks]))+":ordered","--bind-to","core","--report-bindings"]
for ranks in (1,2,4,8,16):
 for version in ("before","after"):
  r=subprocess.run([*mpi(ranks),"-n",str(ranks),str(E/"bin"/("reduction-"+version)),"10000"],env=ENV,stdin=subprocess.DEVNULL,capture_output=True,text=True,timeout=120)
  (STORE/f"expanded-reduction-{version}-p{ranks}.log").write_text(r.stdout+r.stderr)
  assert r.returncode==0,r.stderr
  report["microbenchmarks"].append({"ranks":ranks,"version":version,"samples":[json.loads(l) for l in r.stdout.splitlines() if l.startswith("{")]})
for name,shape,steps in (("smooth2d",(32,8,0),64),("smooth3d",(32,1,1),32)):
 root=E/"expanded"/name;root.mkdir(parents=True,exist_ok=False)
 baseline=root/"baseline";launch(baseline,shape,steps,0,baseline=True)
 reference=comparison.snapshots(baseline/"output")
 archive_lane(baseline)
 case={"name":name,"shape":shape,"steps":steps,"samples":[]};report["cases"].append(case)
 for repeat in range(6):
  order=(1,2,4,8,16) if repeat%2==0 else (16,8,4,2,1)
  for ranks in order:
   lane=root/f"repeat{repeat}-p{ranks}"
   launch(lane,shape,steps,ranks,common=mpi(ranks),executable=E/"bin/ARCH-after")
   case["samples"].append({"repeat":repeat,"phase":"warmup" if repeat==0 else "measured","ranks":ranks,"comparison":check(reference,lane),"metrics":metrics(lane)})
   archive_lane(lane)
   (STORE/"expanded-summary.json").write_text(json.dumps(report,indent=2))
   print(name,repeat,ranks,"passed",flush=True)
 case["statistics"]={}
 for ranks in (1,2,4,8,16):
  samples=[s["metrics"]["critical_driver_nonoutput_seconds"] for s in case["samples"] if s["phase"]=="measured" and s["ranks"]==ranks]
  case["statistics"][str(ranks)]={"median":statistics.median(samples),"min":min(samples),"max":max(samples),"cv":statistics.stdev(samples)/statistics.mean(samples),"n":len(samples)}
 base=case["statistics"]["1"]["median"]
 for ranks,stats in case["statistics"].items():stats["speedup"]=base/stats["median"];stats["efficiency"]=stats["speedup"]/int(ranks)
 (STORE/"expanded-summary.json").write_text(json.dumps(report,indent=2))
report["complete"]=True
(STORE/"expanded-summary.json").write_text(json.dumps(report,indent=2))
print("COMPLETE",flush=True)
