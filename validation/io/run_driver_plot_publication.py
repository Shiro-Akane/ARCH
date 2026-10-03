#!/usr/bin/env python3
"""Linux manual scoped regression using an already built CPU ARCH object set.
No configure, simulation, shell evaluation, or new CI matrix. Compile/link
commands come only from the caller's trusted CMake/Ninja build tree.
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
compile_args=shlex.split(main["command"])
if "-DARCH_CUDA_BUILD_ENABLED=0" not in compile_args:p.error("CPU build required")
out.mkdir(parents=True)
obj=out/"fixture.o";exe=out/"driver-plot-publication"
compile_args[compile_args.index("-o")+1]=str(obj)
compile_args[compile_args.index("-c")+1]=str(root/"tests/host/io/test_driver_plot_publication.cpp")
commands=subprocess.check_output(["ninja","-t","commands","ARCH"],cwd=build,text=True)
link_line=next(line for line in reversed(commands.splitlines()) if " -o bin/ARCH " in line)
# Strip known Ninja no-op scaffolding, reject any remaining shell syntax.
tokens=shlex.split(link_line)
if tokens[:2]==[":","&&"]:tokens=tokens[2:]
if tokens[-2:]==["&&",":"]:tokens=tokens[:-2]
if any(t in {"&&",";","|",">","<"} for t in tokens):p.error("unsupported link scaffolding")
main_object=main["output"]
tokens[tokens.index(main_object)]=str(obj)
tokens[tokens.index("-o")+1]=str(exe)
tokens+=["-Wl,--wrap=H5Dwrite","-Wl,--wrap=H5Fflush","-Wl,--wrap=H5Fclose"]
for name,args in [("compile",compile_args),("link",tokens)]:
    with (out/(name+".log")).open("w") as log:
        process=subprocess.run(args,cwd=build,stdout=log,stderr=subprocess.STDOUT,timeout=180)
    if process.returncode:
        print((out/(name+".log")).read_text());raise SystemExit(process.returncode)
env={**os.environ,"OMP_NUM_THREADS":"1","CUDA_VISIBLE_DEVICES":""}
process=subprocess.run([str(exe),str(out/"evidence")],env=env,text=True,capture_output=True,timeout=30)
(out/"stdout.log").write_text(process.stdout);(out/"stderr.log").write_text(process.stderr)
result={"scope":"real CPU DriverIO IO-only fixture; no timestep or EOS science acceptance",
        "exitCode":process.returncode,"stdout":process.stdout,"stderr":process.stderr,
        "buildDirectory":str(build),
        "driverSourceSha256":hashlib.sha256((root/"src/driver/io/DriverIO.cpp").read_bytes()).hexdigest(),
        "dispatchLibrarySha256":hashlib.sha256((build/"libarch_solver_dispatch.a").read_bytes()).hexdigest(),
        "testExecutableSha256":hashlib.sha256(exe.read_bytes()).hexdigest(),"fixtureSource":str(root/"tests/host/io/test_driver_plot_publication.cpp")}
(out/"result.json").write_text(json.dumps(result,indent=2)+"\n")
print(json.dumps(result));raise SystemExit(process.returncode)
