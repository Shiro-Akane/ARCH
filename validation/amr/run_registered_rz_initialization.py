#!/usr/bin/env python3
"""Compile authoritative initial-population fixture from trusted CPU build commands.
No configure, timestep evolution or scientific output.
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
sources=["tests/host/grid/test_registered_rz_initialization.cpp",
 "src/core/problem/ProblemHelper.cpp","src/physics/eos/eosdispatch.cpp",
 "simulation/GaussianPulse/Gaussian.cpp"]
objects=[]
def execute(label,args):
    with (out/(label+".log")).open("w") as log:
        result=subprocess.run(args,cwd=build,stdout=log,stderr=subprocess.STDOUT,timeout=180)
    if result.returncode:
        print((out/(label+".log")).read_text());raise SystemExit(result.returncode)
for i,source in enumerate(sources):
    entry=next((e for e in entries if pathlib.Path(e["file"]).resolve()==(root/source).resolve()),main)
    args=shlex.split(entry["command"])
    if source=="simulation/GaussianPulse/Gaussian.cpp":
        digest=hashlib.sha256((root/source).read_bytes()).hexdigest()
        if not any("ARCH_CASE_SOURCE_SHA256" in t and digest in t for t in args):
            p.error("existing case compile command source digest does not match actual source")
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
case=next(e for e in entries if pathlib.Path(e["file"]).resolve()==(root/"simulation/GaussianPulse/Gaussian.cpp").resolve())
idx=tokens.index(main["output"]);tokens[idx:idx+1]=objects[:-1]
if case["output"] in tokens:
    tokens[tokens.index(case["output"])]=objects[-1]
else:
    tokens.append(objects[-1])
exe=out/"registered-rz-initialization";tokens[tokens.index("-o")+1]=str(exe)
execute("link",tokens)
result=subprocess.run([str(exe),str(root/"validation/amr/inputs/gaussian_rz_init_probe.par")],env={**os.environ,"OMP_NUM_THREADS":"2","CUDA_VISIBLE_DEVICES":""},
    text=True,capture_output=True,timeout=30)
(out/"stdout.log").write_text(result.stdout);(out/"stderr.log").write_text(result.stderr)
summary={"scope":"Actual registered Gaussian Setup/Init/shared EOS RZ profile; no timestep evolution",
 "exitCode":result.returncode,"stdout":result.stdout,"stderr":result.stderr,
 "buildDirectory":str(build),"executableSha256":hashlib.sha256(exe.read_bytes()).hexdigest(),
 "recompiledSources":{s:hashlib.sha256((root/s).read_bytes()).hexdigest() for s in sources},
 "initializationHeaders":{h:hashlib.sha256((root/h).read_bytes()).hexdigest()
    for h in ["src/interface/ProblemGenerator.h","src/grid/Grid.h","simulation/GaussianPulse/Gaussian.cpp","validation/amr/inputs/gaussian_rz_init_probe.par",
              "src/core/problem/InitialStateConversion.h"]},
 "limitations":["Public RZ dispatch still gated","No registered model science acceptance","CUDA not qualified"]}
(out/"result.json").write_text(json.dumps(summary,indent=2)+"\n")
print(json.dumps(summary));raise SystemExit(result.returncode)
