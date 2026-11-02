"""Regression for the MPI-only reduction transport change."""
from pathlib import Path
import subprocess,json,os,time,shutil
P=Path("/home/ubuntu/projects/ARCH-mpi")
B=Path("/home/ubuntu/projects/ARCH-mpi-baseline")
E=Path("/home/ubuntu/projects/ARCH-mpi-evidence-optimization-20261101")
T=Path("/home/ubuntu/projects/ARCH-mpi-toolchain/env")
env=dict(os.environ,PATH=str(T/"bin")+":"+os.environ["PATH"],LD_LIBRARY_PATH=str(T/"lib"),OMP_NUM_THREADS="1",OPENBLAS_NUM_THREADS="1",PYTHONDONTWRITEBYTECODE="1")
records=[]
def execute(name,cmd):
 path=E/"regression"/name;path.mkdir(parents=True,exist_ok=False)
 start=time.time()
 with (path/"run.log").open("w") as f:
  r=subprocess.run(cmd,env=env,stdin=subprocess.DEVNULL,stdout=f,stderr=subprocess.STDOUT,timeout=1200)
 records.append({"name":name,"command":cmd,"seconds":time.time()-start,"returncode":r.returncode})
 (E/"regression-status.json").write_text(json.dumps(records,indent=2))
 if r.returncode:raise RuntimeError(name)
 print(name,"passed",flush=True)
for mode in ("release","debug"):
 build=P/("build-gpu-"+mode)
 execute(mode+"-ctest",[str(T/"bin/ctest"),"--test-dir",str(build),"--output-on-failure","-R","^(mpi_fixed_grid_[124]|same_level_exchange_plan|reduction_contract)$"])
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
