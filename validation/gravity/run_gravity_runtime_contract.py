#!/usr/bin/env python3
"""Compile an actual CPU Runtime fixture from trusted existing CMake commands.
Recompile the touched Runtime translation units; do not configure, simulate,
generate scientific output, or use old Runtime objects as evidence.
"""
import argparse,json,pathlib,shlex,subprocess,os,hashlib
p=argparse.ArgumentParser(description=__doc__)
p.add_argument("--build",type=pathlib.Path,required=True)
p.add_argument("--output-root",type=pathlib.Path,required=True)
p.add_argument("--native-rz",action="store_true",help="Explicit internal CPU RZ candidate, no physical grant")
a=p.parse_args();build=a.build.resolve();out=a.output_root.resolve()
if out.exists():p.error("output-root must be new")
entries=json.loads((build/"compile_commands.json").read_text())
main=next(e for e in entries if pathlib.Path(e["file"]).name=="main.cpp")
root=pathlib.Path(main["file"]).parent.parent
if "-DARCH_CUDA_BUILD_ENABLED=0" not in shlex.split(main["command"]):p.error("CPU build required")
out.mkdir(parents=True)
sources=["tests/host/gravity/test_gravity_runtime_rz_contract.cpp" if a.native_rz else "tests/host/gravity/test_gravity_runtime_contract.cpp",
 "src/driver/stages/GravityStage.cpp",
 "src/driver/runtime/DriverRuntime.cpp","src/driver/runtime/DriverBoundary.cpp",
 "src/driver/runtime/DriverRegrid.cpp","src/amr/elliptic/EllipticMeshAdapter.cpp"]
objects=[]
def execute(label,args):
    with (out/(label+".log")).open("w") as log:
        result=subprocess.run(args,cwd=build,stdout=log,stderr=subprocess.STDOUT,timeout=180)
    if result.returncode:
        print((out/(label+".log")).read_text());raise SystemExit(result.returncode)
for i,source in enumerate(sources):
    args=shlex.split(main["command"])
    obj=out/(str(i)+".o");objects.append(str(obj))
    args[args.index("-o")+1]=str(obj)
    args[args.index("-c")+1]=str(root/source)
    execute("compile-"+str(i),args)
commands=subprocess.check_output(["ninja","-t","commands","ARCH"],cwd=build,text=True)
line=next(x for x in reversed(commands.splitlines()) if " -o bin/ARCH " in x)
tokens=shlex.split(line)
if tokens[:2]==[":","&&"]:tokens=tokens[2:]
if tokens[-2:]==["&&",":"]:tokens=tokens[:-2]
if any(t in {"&&",";","|",">","<"} for t in tokens):p.error("unsupported link scaffolding")
idx=tokens.index(main["output"]);tokens[idx:idx+1]=objects
exe=out/"gravity-runtime-contract";tokens[tokens.index("-o")+1]=str(exe)
execute("link",tokens)
result=subprocess.run([str(exe),str(out/"runtime-output")],env={**os.environ,"OMP_NUM_THREADS":"2","CUDA_VISIBLE_DEVICES":""},
    text=True,capture_output=True,timeout=1200 if a.native_rz else 30)
(out/"stdout.log").write_text(result.stdout);(out/"stderr.log").write_text(result.stderr)
summary={"scope":"Actual Cartesian CPU Runtime -> GravityStage -> SelfGravity all-block/slot/regrid publication; no simulation time advancement",
 "exitCode":result.returncode,"stdout":result.stdout,"stderr":result.stderr,
 "buildDirectory":str(build),"executableSha256":hashlib.sha256(exe.read_bytes()).hexdigest(),
 "recompiledSources":{s:hashlib.sha256((root/s).read_bytes()).hexdigest() for s in sources},
 "timestepHeaderSha256":hashlib.sha256((root/"src/driver/stages/DriverStages.h").read_bytes()).hexdigest(),
 "rklHeaderSha256":{str(h.relative_to(root)):hashlib.sha256(h.read_bytes()).hexdigest()
    for h in [root/"src/numerics/diffusion"/name for name in
      ["DiffDispatch.h","DiffusionAMRStages.h","RKL1TimeIntegrator.h","RKL2TimeIntegrator.h"]]},
 "limitations":["Supported Cartesian identity path only; RZ production gravity/regrid remains gated","No actual Hydro integration or scientific evolution acceptance","No CUDA qualification; only local gravity diagnostic output"]}
if a.native_rz:
    summary["scope"]="Actual CPU RZ DriverRuntime lease -> GravityStage -> SelfGravity candidate; no timestep"
    summary["limitations"]=["Native candidate only; ordinary physical readers and RZ regrid/Device gates held",
        "No continuous Phi/force or evolution/conservation acceptance"]
(out/"result.json").write_text(json.dumps(summary,indent=2)+"\n")
print(json.dumps(summary));raise SystemExit(result.returncode)
