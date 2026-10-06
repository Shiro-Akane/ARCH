#!/usr/bin/env python3
"""Build a private initial-population fixture from frozen CPU Runtime/leaf owners.
No configure, production rebuild, timestep evolution or scientific output.
"""
import argparse,json,pathlib,subprocess,os,sys
root=pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0,str(root/"tools"))
from validation_fixture_build import build_cpu_fixture,verify_fixture_inputs,RUNTIME_SOURCES
p=argparse.ArgumentParser(description=__doc__)
p.add_argument("--build",type=pathlib.Path,required=True)
p.add_argument("--output-root",type=pathlib.Path,required=True)
a=p.parse_args();build=a.build.resolve();out=a.output_root.resolve()
if out.exists():p.error("output-root must be new")
source="tests/host/grid/test_rz_initial_population.cpp"
providers={name:"ARCH" for name in ["src/core/problem/ProblemHelper.cpp",
    "src/physics/eos/eosdispatch.cpp","src/core/files/FileFingerprint.cpp",
    "src/physics/eos/sources/TabularBaryonSource.cpp","src/physics/eos/sources/TabularCompletion.cpp",
    "src/physics/eos/sources/Tabular3DEOS.cpp","src/physics/eos/sources/Tabular4DEOS.cpp"]}
headers=["src/interface/ProblemGenerator.h","src/grid/Grid.h",
         "src/core/problem/InitialStateConversion.h"]
exe,build_record,frozen=build_cpu_fixture(build=build,output=out,source=root/source,
    executable_name="rz-initial-population",owner_sources=list(RUNTIME_SOURCES),
    observed_headers=headers,reuse_compiled_sources=providers,compile_recipe="production")
result=subprocess.run([str(exe)],env={**os.environ,"OMP_NUM_THREADS":"2","CUDA_VISIBLE_DEVICES":""},
    text=True,capture_output=True,timeout=30)
(out/"stdout.log").write_text(result.stdout);(out/"stderr.log").write_text(result.stderr)
verify_fixture_inputs(frozen,exe,build_record["executableIdentity"])
identities=build_record["inputs"]["files"]
def source_sha(name):return identities[str((root/name).resolve())]["sha256"]
summary={"scope":"Actual shared PopulateState/EOS coordinate/repair population; no timestep evolution",
 "exitCode":result.returncode,"stdout":result.stdout,"stderr":result.stderr,
 "buildDirectory":str(build),"executableSha256":build_record["executableIdentity"]["sha256"],
 "recompiledSources":{source:source_sha(source)},
 "reusedCompiledOwner":build_record["reusedCompiledOwner"],
 "reusedCompiledSources":build_record["reusedCompiledSources"],
 "fixtureBuildInputs":"fixture-build-inputs.json",
 "initializationHeaders":{h:source_sha(h) for h in headers},
 "limitations":["Public RZ dispatch still gated","No registered model science acceptance","CUDA not qualified"]}
(out/"result.json").write_text(json.dumps(summary,indent=2)+"\n")
print(json.dumps(summary));raise SystemExit(result.returncode)
