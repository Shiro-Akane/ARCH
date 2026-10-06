#!/usr/bin/env python3
"""Linux manual scoped regression using an already built CPU ARCH object set.
No configure, simulation, shell evaluation, or new CI matrix. Compile/link
commands come only from the caller's trusted CMake/Ninja build tree.
"""
import argparse,json,pathlib,shlex,subprocess,os,hashlib
p=argparse.ArgumentParser(description=__doc__)
p.add_argument("--build",type=pathlib.Path,required=True)
p.add_argument("--output-root",type=pathlib.Path,required=True)
p.add_argument("--compile-only",action="store_true",help="Build IO-only fixture without running existing fault cases")
a=p.parse_args();build=a.build.resolve();out=a.output_root.resolve()
if out.exists():p.error("output-root must be new")
entries=json.loads((build/"compile_commands.json").read_text())
main=next(e for e in entries if pathlib.Path(e["file"]).name=="main.cpp")
root=pathlib.Path(main["file"]).parent.parent
compile_args=shlex.split(main["command"])
if "-DARCH_CUDA_BUILD_ENABLED=0" not in compile_args:p.error("CPU build required")
out.mkdir(parents=True)
obj=out/"fixture.o";exe=out/"driver-plot-publication"
# Compile the current writer into a separate object, avoiding stale cached IO
# without rebuilding ARCH or mutating the trusted build tree.
plot=next(e for e in entries if pathlib.Path(e["file"]).name=="PlotIO.cpp")
plot_args=shlex.split(plot["command"])
plot_obj=out/"PlotIO.o"
plot_args[plot_args.index("-o")+1]=str(plot_obj)
driver=next(e for e in entries if pathlib.Path(e["file"]).name=="DriverIO.cpp")
driver_args=shlex.split(driver["command"])
driver_obj=out/"DriverIO.o"
driver_args[driver_args.index("-o")+1]=str(driver_obj)
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
# Some build layouts link these sources directly, others through an archive.
# Replace an existing direct object; prepend only when archive selection owns it.
for entry,fresh in ((driver,driver_obj),(plot,plot_obj)):
    original=entry["output"]
    if original in tokens:tokens[tokens.index(original)]=str(fresh)
    else:tokens[1:1]=[str(fresh)]
tokens+=["-Wl,--wrap=H5Dwrite","-Wl,--wrap=H5Fflush","-Wl,--wrap=H5Fclose"]
for name,args in [("compile",compile_args),("compile-plot",plot_args),("compile-driver",driver_args),("link",tokens)]:
    with (out/(name+".log")).open("w") as log:
        process=subprocess.run(args,cwd=build,stdout=log,stderr=subprocess.STDOUT,timeout=180)
    if process.returncode:
        print((out/(name+".log")).read_text());raise SystemExit(process.returncode)
if a.compile_only:
    print(json.dumps({"status":"BUILT_NOT_RUN","testExecutable":str(exe),"testExecutableSha256":hashlib.sha256(exe.read_bytes()).hexdigest(),
        "fixtureSourceSha256":hashlib.sha256((root/"tests/host/io/test_driver_plot_publication.cpp").read_bytes()).hexdigest()}))
    raise SystemExit(0)
env={**os.environ,"OMP_NUM_THREADS":"1","CUDA_VISIBLE_DEVICES":""}
process=subprocess.run([str(exe),str(out/"evidence")],env=env,text=True,capture_output=True,timeout=30)
(out/"stdout.log").write_text(process.stdout);(out/"stderr.log").write_text(process.stderr)
result={"scope":"real CPU DriverIO IO-only fixture; no timestep or EOS science acceptance",
        "exitCode":process.returncode,"stdout":process.stdout,"stderr":process.stderr,
        "buildDirectory":str(build),
        "plotSourceSha256":hashlib.sha256(pathlib.Path(plot["file"]).read_bytes()).hexdigest(),
        "fixtureSourceSha256":hashlib.sha256((root/"tests/host/io/test_driver_plot_publication.cpp").read_bytes()).hexdigest(),
        "driverObjectSha256":hashlib.sha256(driver_obj.read_bytes()).hexdigest(),
        "plotObjectSha256":hashlib.sha256(plot_obj.read_bytes()).hexdigest(),
        "sourceGitHead":subprocess.check_output(["git","rev-parse","HEAD"],cwd=root,text=True).strip(),
        "sourceGitDirty":bool(subprocess.check_output(["git","status","--porcelain"],cwd=root,text=True).strip()),
        "driverSourceSha256":hashlib.sha256((root/"src/driver/io/DriverIO.cpp").read_bytes()).hexdigest(),
        "dispatchLibrarySha256":hashlib.sha256((build/"libarch_solver_dispatch.a").read_bytes()).hexdigest(),
        "testExecutableSha256":hashlib.sha256(exe.read_bytes()).hexdigest(),"fixtureSource":str(root/"tests/host/io/test_driver_plot_publication.cpp")}
(out/"result.json").write_text(json.dumps(result,indent=2)+"\n")
print(json.dumps(result));raise SystemExit(process.returncode)
