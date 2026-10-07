#!/usr/bin/env python3
"""Build the internal CPU RK2 checkpoint fixture with frozen owner objects.
The explicit warm mode runs four physical modules and a genuine AMR regrid.
No configure, production rebuild or public case run. Raw checkpoints stay local.
"""
import argparse,json,pathlib,subprocess,os,sys,time,hashlib
root=pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0,str(root/"tools"))
from validation_fixture_build import build_cpu_fixture,verify_fixture_inputs,RUNTIME_SOURCES
p=argparse.ArgumentParser(description=__doc__)
p.add_argument("--build",type=pathlib.Path,required=True)
p.add_argument("--output-root",type=pathlib.Path,required=True)
p.add_argument("--initial-thermal-rejection",action="store_true",help="Reject an initial native thermal state below the unchanged configured bound")
p.add_argument("--warm-native-active",type=pathlib.Path,help="Explicit maintained warm Helm/aprox13 mixed-AMR checkpoint mode; actual table required")
a=p.parse_args()
if a.warm_native_active and a.initial_thermal_rejection:p.error("warm and initial-rejection modes are exclusive")
started=time.monotonic();table=a.warm_native_active.resolve() if a.warm_native_active else None
def table_identity(path):
    st=path.stat();digest=hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda:stream.read(1024*1024),b""):digest.update(chunk)
    after=path.stat()
    fields=lambda s:(s.st_dev,s.st_ino,s.st_size,s.st_mtime_ns,s.st_ctime_ns)
    if fields(st)!=fields(after):raise RuntimeError("actual Helm table changed during capture")
    return {"path":str(path),"sha256":digest.hexdigest(),"stat":fields(st)}
if table and not table.is_file():p.error("actual warm Helm table must be a regular existing file")
table_before=table_identity(table) if table else None
build=a.build.resolve();out=a.output_root.resolve()
if out.exists():p.error("output-root must be new")
source="tests/host/driver/test_rz_checkpoint_continuation.cpp"
providers={name:"ARCH" for name in ["src/io/chk/ChkIO.cpp",
    "src/io/chk/CheckpointCompatibility.cpp","src/io/hdf5/HDF5Writer.cpp","src/io/plot/PlotIO.cpp",
    "src/core/files/FileFingerprint.cpp","src/core/files/BuildIdentity.cpp",
    "src/core/config/ConfigurationIdentity.cpp","src/physics/eos/eosdispatch.cpp",
    "src/physics/eos/sources/TabularBaryonSource.cpp","src/physics/eos/sources/TabularCompletion.cpp",
    "src/physics/eos/sources/Tabular3DEOS.cpp","src/physics/eos/sources/Tabular4DEOS.cpp"]}
providers["src/driver/io/DriverIO.cpp"]="arch_solver_dispatch"
headers=["tests/math/io/PlotIdentityFixture.h","src/driver/stages/DriverStages.h",
         "src/driver/runtime/DriverRuntime.h","src/numerics/state/RzNativeClosure.h"]
if table:headers += ["tests/host/gravity/NativeActiveFourModuleWitness.h",
    "tests/host/gravity/NativeActiveAmrWitness.h","tests/host/driver/RzRuntimeWitness.h",
    "src/physics/eos/HelmEos.h","src/physics/network/aprox13/NetAprox13.h",
    "src/numerics/burnsolver/ode/ode_bd.h","src/numerics/diffusion/DiffusionAMRStages.h",
    "src/numerics/diffusion/DiffFlux.h","src/driver/stages/DriverMacroStep.h",
    "src/physics/gravity/NativeSelfStage.h","src/physics/gravity/self/SelfGravity.h",
    "src/driver/stages/GravityStage.h"]
exe,build_record,frozen=build_cpu_fixture(build=build,output=out,source=root/source,
    executable_name="rz-checkpoint-continuation",owner_sources=list(RUNTIME_SOURCES),
    observed_headers=headers,reuse_compiled_sources=providers,
    compile_recipe="production",io_fixture_identity=True)
run_args=[str(exe),str(out/"evidence")]
if table:run_args += ["--warm-native-active",str(table)]
elif a.initial_thermal_rejection:run_args += ["--initial-thermal-rejection"]
remaining=2400.-(time.monotonic()-started) if table else 30.
if remaining<=0.:raise RuntimeError("warm compile+run 2400s envelope exhausted before run")
try:
    result=subprocess.run(run_args,env={**os.environ,"OMP_NUM_THREADS":"2","CUDA_VISIBLE_DEVICES":""},
        text=True,capture_output=True,timeout=remaining)
except subprocess.TimeoutExpired as error:
    def partial(value):return value.decode(errors="replace") if isinstance(value,bytes) else value or ""
    (out/"stdout.log").write_text(partial(error.stdout));(out/"stderr.log").write_text(partial(error.stderr))
    (out/"timeout.json").write_text(json.dumps({"warmNativeActive":bool(table),
        "runTimeoutSeconds":remaining,"compileAndRunSeconds":time.monotonic()-started,
        "scienceQualification":False},indent=2)+"\n")
    raise
if table and table_identity(table)!=table_before:raise RuntimeError("actual warm Helm table changed during fixture")
(out/"stdout.log").write_text(result.stdout);(out/"stderr.log").write_text(result.stderr)
verify_fixture_inputs(frozen,exe,build_record["executableIdentity"])
identities=build_record["inputs"]["files"]
def source_sha(name):return identities[str((root/name).resolve())]["sha256"]
summary={"scope":("Internal RZ warm four-module M0 -> actual regrid/Current -> M1 -> checkpoint -> fresh empty Runtime -> uninterrupted/resumed M2; engineering and bitwise continuation fixture, no continuous-energy qualification"
    if table else "Internal RZ actual Host RK2 -> checkpoint -> reconstructed Runtime -> next RK2 step; engineering fixture, no public simulation"),
 "exitCode":result.returncode,"stdout":result.stdout,"stderr":result.stderr,
 "initialThermalRejectionProbe":a.initial_thermal_rejection,
 "warmNativeActive":bool(table),"actualHelmTableIdentity":table_before,
 "compileAndRunSeconds":time.monotonic()-started,
 "prospectiveCompileAndRunCapSeconds":2400 if table else None,
 "stageHeaderSha256":source_sha("src/driver/stages/DriverStages.h"),
 "buildDirectory":str(build),"executableSha256":build_record["executableIdentity"]["sha256"],
 "recompiledSources":{source:source_sha(source)},
 "reusedCompiledOwner":build_record["reusedCompiledOwner"],
 "reusedCompiledSources":build_record["reusedCompiledSources"],
 "fixtureBuildInputs":"fixture-build-inputs.json",
 "limitations":["Public RZ dispatch still gated","No scientific evolution acceptance","CUDA not qualified",
    "Warm mode requires actual dynamic M1 acceptance; neither old fixture nor materialized field alone qualifies it",
    "Warm compile+run wall deadline counts compilation; an outer existing resource guard supplies hard process-tree/disk enforcement"]}
(out/"result.json").write_text(json.dumps(summary,indent=2)+"\n")
print(json.dumps(summary));raise SystemExit(result.returncode)
