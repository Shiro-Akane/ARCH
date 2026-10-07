#!/usr/bin/env python3
"""Compile a private CPU Runtime fixture with the existing CTest link owner.
Require frozen/fresh production libraries. Do not configure, rebuild Runtime,
simulate, generate scientific output or claim public native-RZ qualification.
"""
import argparse,json,pathlib,subprocess,os,sys
root=pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0,str(root/"tools"))
from validation_fixture_build import build_cpu_fixture,verify_fixture_inputs
p=argparse.ArgumentParser(description=__doc__)
p.add_argument("--build",type=pathlib.Path,required=True)
p.add_argument("--output-root",type=pathlib.Path,required=True)
a=p.parse_args();build=a.build.resolve();out=a.output_root.resolve()
if out.exists():p.error("output-root must be new")
source="tests/host/driver/test_rz_runtime_boundary.cpp"
owners=["src/driver/runtime/DriverRuntime.cpp","src/driver/runtime/DriverBoundary.cpp",
        "src/driver/runtime/DriverBoundaryDiagnostics.cpp",
        "src/physics/boundary/PhysicalBoundaryHandler.cpp",
        "src/driver/runtime/DriverRegrid.cpp"]
headers=["tests/host/driver/RzRuntimeWitness.h","src/driver/stages/DriverStages.h","src/driver/runtime/DriverRuntime.h",
         "src/driver/schedule/StageScheduler.h","src/numerics/state/RzNativeClosure.h"]
rkl_headers=["src/numerics/diffusion/"+name for name in
    ["DiffDispatch.h","DiffusionAMRStages.h","RKL1TimeIntegrator.h","RKL2TimeIntegrator.h"]]
exe,build_record,frozen=build_cpu_fixture(build=build,output=out,source=root/source,
    executable_name="rz-runtime-boundary",owner_sources=owners,
    observed_headers=headers+rkl_headers)
result=subprocess.run([str(exe)],env={**os.environ,"OMP_NUM_THREADS":"2","CUDA_VISIBLE_DEVICES":""},
    text=True,capture_output=True,timeout=30)
(out/"stdout.log").write_text(result.stdout);(out/"stderr.log").write_text(result.stderr)
verify_fixture_inputs(frozen,exe,build_record["executableIdentity"])
identities=build_record["inputs"]["files"]
def source_sha(name):return identities[str((root/name).resolve())]["sha256"]
summary={"scope":"Actual CPU Runtime halo, timestep candidates and RKL stage kernels; no simulation driver time advancement or scientific output",
 "exitCode":result.returncode,"stdout":result.stdout,"stderr":result.stderr,
 "buildDirectory":str(build),"executableSha256":build_record["executableIdentity"]["sha256"],
 "recompiledSources":{source:source_sha(source)},
 "reusedCompiledOwner":build_record["reusedCompiledOwner"],
 "fixtureBuildInputs":"fixture-build-inputs.json",
 "timestepHeaderSha256":source_sha("src/driver/stages/DriverStages.h"),
 "rklHeaderSha256":{h:source_sha(h) for h in rkl_headers},
 "limitations":["RZ production regrid remains gated","CUDA not qualified","No scientific evolution acceptance"]}
(out/"result.json").write_text(json.dumps(summary,indent=2)+"\n")
print(json.dumps(summary));raise SystemExit(result.returncode)
