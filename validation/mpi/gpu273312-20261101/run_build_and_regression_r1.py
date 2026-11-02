from pathlib import Path
import os,json,subprocess,datetime,time,hashlib,shutil,traceback
R=Path("/home/ubuntu/projects/ARCH-mpi")
B=Path("/home/ubuntu/projects/ARCH-mpi-baseline")
E=Path("/home/ubuntu/projects/ARCH-mpi-evidence-gpu273312-20261101")
T=Path("/home/ubuntu/projects/ARCH-mpi-toolchain/env")
env=dict(os.environ,PATH=str(T/"bin")+":"+os.environ["PATH"],CC=str(T/"bin/x86_64-conda-linux-gnu-cc"),CXX=str(T/"bin/x86_64-conda-linux-gnu-c++"),CMAKE_PREFIX_PATH=str(T),OMP_NUM_THREADS="1",OPENBLAS_NUM_THREADS="1",PYTHONDONTWRITEBYTECODE="1",GIT_LFS_SKIP_SMUDGE="1")
report={"source_commit":"29972a167c9bc0bb77bdb07557f920f54bfad64f","commands":[],"variants":{}}
def now():return datetime.datetime.now(datetime.timezone.utc).isoformat()
def save(): (E/"execution-status-r1.json").write_text(json.dumps(report,indent=2))
def run(name,cmd,timeout=1800):
 assert shutil.disk_usage(E).free>24*2**30,"free space below 24 GiB"
 rec={"name":name,"command":[str(x) for x in cmd],"started":now()}
 report["commands"].append(rec);save();print("START",name,flush=True)
 start=time.monotonic()
 with (E/(name+".log")).open("w") as f:
  p=subprocess.run(cmd,env=env,cwd=R,stdin=subprocess.DEVNULL,stdout=f,stderr=subprocess.STDOUT,timeout=timeout)
 rec.update(returncode=p.returncode,seconds=time.monotonic()-start,finished=now());save()
 if p.returncode:
  print((E/(name+".log")).read_text()[-4000:],flush=True);raise RuntimeError(name+" failed")
 print("PASS",name,round(rec["seconds"],2),flush=True)
try:
 for config in ["Debug","Release"]:
  label=config.lower();dirs={}
  for lane,src,mpi in [("baseline",B,False),("mpi",R,True),("serial",R,False)]:
   dest=src/("build-gpu-"+label+("-serial" if lane=="serial" else ""))
   if dest.exists():
    assert (dest/"CMakeCache.txt").is_file()
    assert ("CMAKE_HOME_DIRECTORY:INTERNAL="+str(src)) in (dest/"CMakeCache.txt").read_text()
   cmd=["cmake","-S",str(src),"-B",str(dest),"-G","Ninja","-DCMAKE_BUILD_TYPE="+config,"-DARCH_RUNTIME_OUTPUT_DIRECTORY="+str(dest/"bin"),"-DBUILD_TESTING="+("OFF" if lane=="baseline" else "ON"),"-DARCH_ENABLE_KLU=OFF","-DARCH_ENABLE_CUDA=OFF","-DARCH_BUILD_STUDIO=OFF","-DARCH_CUSTOM_NETWORKS=","-DFETCHCONTENT_SOURCE_DIR_HIGHFIVE=/home/ubuntu/projects/ARCH-mpi-dependencies/highfive-src","-DFETCHCONTENT_FULLY_DISCONNECTED=ON"]
   if lane!="baseline":cmd+=["-DARCH_ENABLE_MPI="+("ON" if mpi else "OFF")]
   run("r1-"+label+"-"+lane+"-configure",cmd)
   targets=["ARCH"]
   if lane=="mpi":targets+=["arch_mpi_fixed_grid","arch_same_level_exchange_plan","arch_reduction_contract"]
   if lane=="serial":targets+=["arch_boundary_plan","arch_same_level_exchange_plan","arch_amr_operation_plans","arch_amr_flux_surface_plan","arch_topology_transaction","arch_reduction_contract"]
   run(label+"-"+lane+"-build",["cmake","--build",str(dest),"-j8","--target",*targets],3600)
   dirs[lane]=str(dest)
   if lane!="baseline":
    pattern="^(mpi_fixed_grid_[124]|same_level_exchange_plan|reduction_contract)$" if mpi else "^(boundary_plan|same_level_exchange_plan|amr_operation_plans|amr_flux_surface_plan|topology_transaction|reduction_contract)$"
    run(label+"-"+lane+"-ctest",["ctest","--test-dir",str(dest),"-R",pattern,"--output-on-failure"],300)
   if lane=="serial":
    dep=subprocess.check_output(["ldd",str(dest/"bin/ARCH")],env=env,text=True)
    (E/("r1-"+label+"-serial-ldd.txt")).write_text(dep)
    assert not any("libmpi" in l.lower() for l in dep.splitlines())
  report["variants"][label]={"build_dirs":dirs};save()
  run(label+"-application",[str(T/"bin/python"),str(R/"tests/integration/test_mpi_fixed_grid.py"),"--baseline",dirs["baseline"]+"/bin/ARCH","--candidate",dirs["mpi"]+"/bin/ARCH","--serial-candidate",dirs["serial"]+"/bin/ARCH","--evidence",str(E/(label+"-application"))],1800)
  summary=json.loads((E/(label+"-application/summary.json")).read_text())
  assert len(summary["cases"])==26 and all(x["bitwise_equal"] for x in summary["cases"])
  assert len(summary["rejections"])==7
  report["variants"][label].update(comparisons=26,rejections=7,oversubscribe=False);save()
 report["status"]="short_regression_passed";report["finished"]=now();save()
 print("ALL DEBUG AND RELEASE SHORT REGRESSIONS PASSED",flush=True)
except BaseException as ex:
 report["status"]="failed";report["error"]=repr(ex);report["traceback"]=traceback.format_exc();save();raise
