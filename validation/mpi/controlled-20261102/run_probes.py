#!/usr/bin/env python3
"""Run bounded isolated transport comparisons after application trials finish."""
import argparse, hashlib, json, os, shlex, statistics, subprocess, time
from pathlib import Path
p=argparse.ArgumentParser()
p.add_argument("--evidence",type=Path,required=True)
p.add_argument("--ssh-config",type=Path,required=True)
a=p.parse_args();E=a.evidence.resolve()
T=Path("/home/ubuntu/projects/ARCH-mpi-toolchain/env")
ssh=json.loads(a.ssh_config.read_text())
env=dict(os.environ,PATH=str(T/"bin")+":"+os.environ["PATH"],LD_LIBRARY_PATH=str(T/"lib"),
         OMP_NUM_THREADS="1",OPENBLAS_NUM_THREADS="1")
out={"started":time.time(),"runs":[],"complete":False}
for program,placements in [
    ("transport_probe",[(2,"local"),(2,"cross")]),
    ("halo_protocol_probe",[(2,"local"),(2,"cross"),(4,"cross"),(4,"local")])]:
 for ranks,placement in placements:
  cmd=[str(T/"bin/mpiexec"),"-n",str(ranks),"--bind-to","core","--report-bindings",
       "-x","PATH","-x","LD_LIBRARY_PATH"]
  if placement=="local":
   cmd+=["--map-by","pe-list="+",".join(map(str,range(ranks)))+":ordered"]
  else:
   cmd+=["--host","150.65.181.188:2,150.65.181.189:2","--map-by",f"ppr:{ranks//2}:node",
         "--prtemca","plm_ssh_agent",shlex.join(["/usr/bin/env","-u","LD_LIBRARY_PATH",*ssh]),
         "--prtemca","oob_tcp_include","150.65.181.0/24","--mca","btl","self,tcp",
         "--mca","btl_tcp_if_include","150.65.181.0/24"]
  cmd.append(str(E/"bin"/program))
  result=subprocess.run(cmd,env=env,stdin=subprocess.DEVNULL,capture_output=True,text=True,timeout=120)
  log=E/f"{program}-{placement}-p{ranks}.log";log.write_text(result.stdout+result.stderr)
  assert result.returncode==0, str(log)
  rows=[json.loads(line) for line in result.stdout.splitlines() if line.startswith("{")]
  assert len(rows)==(15 if program=="transport_probe" else 20)
  out["runs"].append({"program":program,"placement":placement,"ranks":ranks,"command":cmd,
        "returncode":result.returncode,"sha256":hashlib.sha256((E/"bin"/program).read_bytes()).hexdigest(),
        "samples":rows})
  (E/"protocol-probes.json").write_text(json.dumps(out,indent=2))
  print(program,placement,ranks,"passed",flush=True)
out["halo_statistics"]=[]
for run in out["runs"]:
 if run["program"]!="halo_protocol_probe":continue
 for size in (24576,196608):
  modes={}
  for concurrent in (False,True):
   values=[r["max_rank_seconds_per_exchange"] for r in run["samples"]
           if r["bytes_per_direction"]==size and r["concurrent"]==concurrent]
   modes[str(concurrent)]={"n":len(values),"median":statistics.median(values),"min":min(values),"max":max(values)}
  out["halo_statistics"].append({"placement":run["placement"],"ranks":run["ranks"],"bytes_per_direction":size,
       "sequential":modes["False"],"concurrent":modes["True"],
       "concurrent_over_sequential":modes["True"]["median"]/modes["False"]["median"]})
out["complete"]=True;out["finished"]=time.time()
(E/"protocol-probes.json").write_text(json.dumps(out,indent=2))
print(json.dumps(out["halo_statistics"]),flush=True)
