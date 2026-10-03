#!/usr/bin/env python3
"""Compile actual CPU Driver/Runtime/IO checkpoint fixture from trusted build commands.
No configure or public case run. Only the existing internal two-step RK2 engineering fixture; raw checkpoints stay local.
"""
import argparse,json,pathlib,shlex,subprocess,os,hashlib
p=argparse.ArgumentParser(description=__doc__)
p.add_argument("--build",type=pathlib.Path,required=True)
p.add_argument("--output-root",type=pathlib.Path,required=True)
a=p.parse_args();build=a.build.resolve();out=a.output_root.resolve()
if out.exists():p.error("output-root must be new")
entries=json.loads((build/"compile_commands.json").read_text())
main=next(e for e in entries if pathlib.Path(e["file"]).name=="main.cpp")
root=pathlib.Path(main["file"]).parent.parent
if "-DARCH_CUDA_BUILD_ENABLED=0" not in shlex.split(main["command"]):p.error("CPU build required")
out.mkdir(parents=True)
sources=["tests/host/driver/test_rz_checkpoint_continuation.cpp",
 "src/driver/runtime/DriverRuntime.cpp","src/driver/runtime/DriverBoundary.cpp",
 "src/driver/runtime/DriverRegrid.cpp","src/driver/io/DriverIO.cpp",
 "src/io/chk/ChkIO.cpp","src/io/chk/CheckpointCompatibility.cpp",
 "src/io/hdf5/HDF5Writer.cpp","src/io/plot/PlotIO.cpp"]
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
exe=out/"rz-checkpoint-continuation";tokens[tokens.index("-o")+1]=str(exe)
execute("link",tokens)
result=subprocess.run([str(exe),str(out/"evidence")],env={**os.environ,"OMP_NUM_THREADS":"2","CUDA_VISIBLE_DEVICES":""},
    text=True,capture_output=True,timeout=30)
(out/"stdout.log").write_text(result.stdout);(out/"stderr.log").write_text(result.stderr)
summary={"scope":"Internal RZ actual Host RK2 -> checkpoint -> reconstructed Runtime -> next RK2 step; engineering fixture, no public simulation",
 "exitCode":result.returncode,"stdout":result.stdout,"stderr":result.stderr,
 "buildDirectory":str(build),"executableSha256":hashlib.sha256(exe.read_bytes()).hexdigest(),
 "recompiledSources":{s:hashlib.sha256((root/s).read_bytes()).hexdigest() for s in sources},
 "limitations":["Public RZ dispatch still gated","No scientific evolution acceptance","CUDA not qualified"]}
(out/"result.json").write_text(json.dumps(summary,indent=2)+"\n")
print(json.dumps(summary));raise SystemExit(result.returncode)
