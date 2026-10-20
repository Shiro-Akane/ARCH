#!/usr/bin/env python3
"""Compile only the selected actual CPU Runtime test from its strict CTest owner.
Reuse authenticated fresh Runtime/gravity libraries and real production owner
objects; do not configure, rebuild production, simulate or grant native physics.
"""
import argparse,json,pathlib,subprocess,os,sys
import hashlib
import math
root=pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0,str(root/"tools"))
from validation_fixture_build import build_cpu_fixture,verify_fixture_inputs,RUNTIME_SOURCES
p=argparse.ArgumentParser(description=__doc__)
p.add_argument("--build",type=pathlib.Path,required=True)
p.add_argument("--output-root",type=pathlib.Path,required=True)
a=p.parse_args()
build=a.build.resolve();out=a.output_root.resolve()
if out.exists():p.error("output-root must be new")
source="tests/host/gravity/test_gravity_runtime_contract.cpp"
# Exact five Runtime owner TUs; real compiled objects are never claimed rebuilt.
owners=list(RUNTIME_SOURCES)
grav_sources=["src/physics/gravity/self/SelfGravity.cpp",
              "src/physics/gravity/GravityBoundary.cpp","src/physics/gravity/GravityExecution.cpp"]
headers=["tools/validation_fixture_build.py",
         "src/physics/gravity/self/SelfGravity.h","src/physics/gravity/self/GravityWorkspace.h",
         "src/physics/gravity/GravityBoundary.h","src/physics/gravity/GravityExecution.h",
         "src/physics/gravity/GravitySolveTypes.h","src/physics/gravity/IGravityPolicy.h",
         "src/driver/stages/GravityStage.h","src/driver/runtime/DriverRuntime.h",
         "src/driver/runtime/StateResidency.h","src/driver/stages/DriverStages.h",
         "src/driver/schedule/StageScheduler.h","src/numerics/state/RzNativeClosure.h",
         "src/amr/elliptic/EllipticMeshAdapter.h"]
rkl_headers=["src/numerics/diffusion/"+name for name in
    ["DiffDispatch.h","DiffusionAMRStages.h","RKL1TimeIntegrator.h","RKL2TimeIntegrator.h"]]
exe,build_record,frozen=build_cpu_fixture(build=build,output=out,source=root/source,
    executable_name="gravity-runtime-contract",owner_sources=owners,
    observed_headers=headers+rkl_headers+grav_sources,compile_recipe="production")
# Keep the original selected-test production compile controls, including LTO/FP
# and explicit OpenMP; linking uses only the exact existing four-test/two-real-
# production CTest owner. All source/library/ELF identities are checked again.
identities=build_record["inputs"]["files"]
def source_sha(name):return identities[str((root/name).resolve())]["sha256"]
reused_libraries={name:identity for name,identity in identities.items()
    if pathlib.Path(name).name in {"libarch_driver_runtime.a","libarch_gravity_cpu.a"}}
if {pathlib.Path(name).name for name in reused_libraries}!={"libarch_driver_runtime.a","libarch_gravity_cpu.a"}:
    raise RuntimeError("actual Runtime/gravity archives absent from the frozen fixture link")
test_args=[str(exe),str(out/"runtime-output")]
result=subprocess.run(test_args,env={**os.environ,"OMP_NUM_THREADS":"2","CUDA_VISIBLE_DEVICES":""},
    text=True,capture_output=True,timeout=30)
(out/"stdout.log").write_text(result.stdout);(out/"stderr.log").write_text(result.stderr)
verify_fixture_inputs(frozen,exe,build_record["executableIdentity"])
summary={"scope":"Actual Cartesian CPU Runtime -> GravityStage -> SelfGravity all-block/slot/regrid publication; no simulation time advancement",
 "exitCode":result.returncode,"stdout":result.stdout,"stderr":result.stderr,
 "buildDirectory":str(build),"executableSha256":build_record["executableIdentity"]["sha256"],
 "recompiledSources":{source:source_sha(source)},
 "reusedCompiledOwner":build_record["reusedCompiledOwner"],
 "retainedCompiledOwnerSources":build_record["retainedCompiledOwnerSources"],
 "reusedGravitySourceSha256":{name:source_sha(name) for name in grav_sources},
 "reusedLibraries":reused_libraries,
 "fixtureBuildInputs":"fixture-build-inputs.json",
 "observedHeaderSha256":{name:source_sha(name) for name in headers},
 "timestepHeaderSha256":source_sha("src/driver/stages/DriverStages.h"),
 "rklHeaderSha256":{name:source_sha(name) for name in rkl_headers},
 "limitations":["This fixture checks the supported Cartesian identity path only","No actual Hydro integration or scientific evolution acceptance","No CUDA qualification; only local gravity diagnostic output"]}
(out/"result.json").write_text(json.dumps(summary,indent=2)+"\n")
print(json.dumps(summary));raise SystemExit(result.returncode)
