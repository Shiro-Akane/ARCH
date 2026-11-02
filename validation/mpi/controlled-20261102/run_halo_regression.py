"""Regress the MPI-only halo candidate against already-validated MPI-OFF controls."""
from pathlib import Path
import subprocess,json,os,time,shutil
P=Path("/home/ubuntu/projects/ARCH-mpi")
B=Path("/home/ubuntu/projects/ARCH-mpi-baseline")
E=Path("/dev/shm/arch-mpi-halo-regression-20261102")
E.mkdir(exist_ok=True)
T=Path("/home/ubuntu/projects/ARCH-mpi-toolchain/env")
env=dict(os.environ,PATH=str(T/"bin")+":"+os.environ["PATH"],LD_LIBRARY_PATH=str(T/"lib"),OMP_NUM_THREADS="1",OPENBLAS_NUM_THREADS="1",PYTHONDONTWRITEBYTECODE="1")
records=json.loads((E/"regression-status.json").read_text()) if (E/"regression-status.json").exists() else []
def execute(name,cmd):
 if any(r["name"]==name and r["returncode"]==0 and r["command"]==cmd for r in records):
  print(name,"already passed",flush=True);return
 path=E/"regression"/name
 if path.exists():
  path.rename(path.with_name(name+"-failed-attempt-"+str(sum(r["name"]==name for r in records))))
 path.mkdir(parents=True,exist_ok=False)
 start=time.time()
 with (path/"run.log").open("w") as f:
  r=subprocess.run(cmd,env=env,stdin=subprocess.DEVNULL,stdout=f,stderr=subprocess.STDOUT,timeout=1200)
 records.append({"name":name,"command":cmd,"seconds":time.time()-start,"returncode":r.returncode})
 (E/"regression-status.json").write_text(json.dumps(records,indent=2))
 if r.returncode:raise RuntimeError(name)
 print(name,"passed",flush=True)
for mode in ("release","debug"):
 build=P/("build-gpu-"+mode)
 execute(mode+"-build-host",[str(T/"bin/cmake"),"--build",str(build),"--target","arch_execution_preflight_limits","arch_same_level_exchange_plan","arch_reduction_contract","-j","2"])
 execute(mode+"-ctest",[str(T/"bin/ctest"),"--test-dir",str(build),"--output-on-failure","-R","^(mpi_fixed_grid_[124]|same_level_exchange_plan|reduction_contract|execution_preflight_contract|execution_preflight_limits)$"])
 for ranks in (8,16):
  execute(mode+f"-host-p{ranks}",[str(T/"bin/mpiexec"),"-n",str(ranks),"--map-by","pe-list="+",".join(map(str,(list(range(12))+[24,25,26,27])[:ranks]))+":ordered","--bind-to","core",str(build/"arch_mpi_fixed_grid")])
 execute(mode+"-1d",[str(T/"bin/python"),"-B",str(P/"tests/integration/test_mpi_fixed_grid.py"),
  "--baseline",str(B/("build-gpu-"+mode)/"bin/ARCH"),"--candidate",str(build/"bin/ARCH"),
  "--serial-candidate",str(P/("build-gpu-"+mode+"-serial")/"bin/ARCH"),
  "--evidence",str(E/"regression"/(mode+"-1d-data")),"--mpiexec",str(T/"bin/mpiexec"),"--mpiexec-arg=--bind-to","--mpiexec-arg=none"])
 execute(mode+"-multidim",[str(T/"bin/python"),"-B",str(P/"tests/integration/test_mpi_multidim.py"),
  "--baseline",str(B/("build-gpu-"+mode)/"bin/ARCH"),"--candidate",str(build/"bin/ARCH"),
  "--serial-candidate",str(P/("build-gpu-"+mode+"-serial")/"bin/ARCH"),
  "--evidence",str(E/"regression"/(mode+"-multidim-data")),"--mpiexec",str(T/"bin/mpiexec")])
print("COMPLETE",flush=True)

# Preserve all logs, parameters and raw HDF5; verify the archive before removing staging.
import hashlib,tarfile
store=Path("/home/ubuntu/projects/ARCH-mpi-evidence-controlled-20261102/halo-candidate")
target=store/"regression-raw.tar.gz"
files={str(p.relative_to(E)):{"bytes":p.stat().st_size,"sha256":hashlib.sha256(p.read_bytes()).hexdigest()}
       for p in E.rglob("*") if p.is_file()}
with tarfile.open(target,"w:gz",compresslevel=3) as tar:tar.add(E,arcname=E.name)
checked=set()
with tarfile.open(target,"r:gz") as tar:
 for member in tar:
  if member.isfile():
   name=str(Path(member.name).relative_to(E.name));data=tar.extractfile(member).read()
   assert len(data)==files[name]["bytes"] and hashlib.sha256(data).hexdigest()==files[name]["sha256"]
   checked.add(name)
assert checked==set(files)
(store/"regression-archive.json").write_text(json.dumps({"archive":target.name,"sha256":hashlib.sha256(target.read_bytes()).hexdigest(),"bytes":target.stat().st_size,"members":files,"verified":True},indent=2))
shutil.copy2(E/"regression-status.json",store/"regression-status.json")
for p in (E/"regression").glob("*-data/summary.json"):
 shutil.copy2(p,store/("regression-"+p.parent.name+"-summary.json"))
assert E.resolve()==Path("/dev/shm/arch-mpi-halo-regression-20261102")
shutil.rmtree(E)
print("ARCHIVE VERIFIED",flush=True)
