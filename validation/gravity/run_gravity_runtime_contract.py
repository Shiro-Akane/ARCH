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
modes=p.add_mutually_exclusive_group()
modes.add_argument("--native-rz",action="store_true",help="Explicit internal CPU RZ candidate, no physical grant")
modes.add_argument("--native-rz-regrid",action="store_true",help="Explicit internal CPU RZ Runtime AMR transaction, no physical grant")
p.add_argument("--materialized-source-only",action="store_true",help="One actual Native source plus candidate field snapshot, separate from lifecycle matrix")
p.add_argument("--matched-resolution",type=int,choices=(0,1,2),default=None,
    help="Maintainer-only root-layout ordinal for native source-only export; omitted means original level 0")
p.add_argument("--field-after-regrid",action="store_true",help="Native RZ regrid plus original candidate fields on refined/coarse topology")
p.add_argument("--regrid-rollback",action="store_true",help="Actual CPU RZ finalizer fault/rollback verification")
a=p.parse_args()
if a.materialized_source_only and not a.native_rz:p.error("--materialized-source-only requires --native-rz")
if a.matched_resolution is not None and not (a.native_rz and a.materialized_source_only):
    p.error("--matched-resolution requires --native-rz --materialized-source-only")
if a.regrid_rollback and not a.native_rz_regrid:p.error("--regrid-rollback requires --native-rz-regrid")
if a.regrid_rollback and a.field_after_regrid:p.error("field-after-regrid and rollback are independent runs")
if a.field_after_regrid and not a.native_rz_regrid:p.error("--field-after-regrid requires --native-rz-regrid")
build=a.build.resolve();out=a.output_root.resolve()
if out.exists():p.error("output-root must be new")
source=("tests/host/gravity/test_rz_runtime_rollback_contract.cpp" if a.regrid_rollback else
 "tests/host/gravity/test_rz_runtime_regrid_contract.cpp" if a.native_rz_regrid else
 "tests/host/gravity/test_gravity_runtime_rz_contract.cpp" if a.native_rz else
 "tests/host/gravity/test_gravity_runtime_contract.cpp")
# Exact five Runtime owner TUs; real compiled objects are never claimed rebuilt.
owners=list(RUNTIME_SOURCES)
grav_sources=["src/physics/gravity/self/SelfGravity.cpp",
              "src/physics/gravity/GravityBoundary.cpp","src/physics/gravity/GravityExecution.cpp"]
headers=["tools/validation_fixture_build.py","tests/host/driver/RzRuntimeWitness.h",
         "tests/host/gravity/RzMaterializedSourceRecord.h",
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
if a.field_after_regrid:test_args.append("--field-after-regrid")
if a.materialized_source_only:test_args.append("--materialized-source-only")
if a.matched_resolution is not None:test_args.extend(["--matched-resolution",str(a.matched_resolution)])
result=subprocess.run(test_args,env={**os.environ,"OMP_NUM_THREADS":"2","CUDA_VISIBLE_DEVICES":""},
    text=True,capture_output=True,timeout=1200 if a.native_rz or a.field_after_regrid or a.regrid_rollback else 30)
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
 "limitations":["Supported Cartesian identity path only; RZ production gravity/regrid remains gated","No actual Hydro integration or scientific evolution acceptance","No CUDA qualification; only local gravity diagnostic output"]}
if a.native_rz or a.native_rz_regrid:
    summary["scope"]="Actual CPU RZ DriverRuntime lease -> GravityStage -> SelfGravity candidate; no timestep"
    summary["limitations"]=["Native candidate only; ordinary physical readers and RZ regrid/Device gates held",
        "No continuous Phi/force or evolution/conservation acceptance"]
if a.native_rz_regrid:
    summary["scope"]="Actual CPU RZ Runtime conservative AMR transactions and authentic cold parent veto; no timestep or gravity field grant"
    summary["limitations"]=["Internal migration only; production RZ/Device gates held",
        "No continuous Phi/force, Hydro evolution or full angular science acceptance"]
if a.field_after_regrid:
    summary["scope"]="Actual CPU RZ Runtime refined/coarse AMR -> native field rebind; no timestep"
    summary["limitations"]=["Explicit candidate only; production physical readers/Device gates held",
        "No continuous Phi/force or Hydro conservation/evolution science acceptance"]
if a.regrid_rollback:
    summary["scope"]="Actual CPU RZ Runtime finalizer fault, source/ledger/pool rollback and retry; no timestep"
    summary["limitations"]=["Injected engineering failure only, not a physical stability/evolution gate",
        "Seven actual source Host vector addresses, values and BC frame are checked through in-place rollback; no unrelated pointer or Device ownership grant",
        "Production RZ/Device gates held"]
if a.materialized_source_only:
    summary["scope"]="Actual CPU Native RZ Runtime initialization -> checked Current source and same candidate field inspection; no timestep"
    summary["limitations"]=["Source and same-solve diagnostic only; lifecycle/fault matrix is a separate default lane",
        "Physical/native/Device gates held; no continuous accuracy or coupled evolution acceptance"]
    # Read real exported binding metadata; this is source/field identity only,
    # not a manufactured ideal observer table or a scientific accuracy gate.
    resolution_level=0 if a.matched_resolution is None else a.matched_resolution
    scale=1 << resolution_level
    summary["matched_resolution"]={"level":resolution_level,"explicitlyRequested":a.matched_resolution is not None,
        "expectedDenseCells":512*scale*scale,"expectedRootBlocks":[2*scale,scale],
        "actualBindingObserved":False,"physicalQualified":False,"scienceAccepted":False,
        "timeAdvanced":False,"scope":"Maintainer-only fixed physical source, changed uniform root layout"}
    if result.returncode==0:
        record_path=out/"runtime-output"/"materialized-native-source.json"
        before=record_path.stat();raw=record_path.read_bytes();after=record_path.stat()
        stat_identity=lambda value:(value.st_dev,value.st_ino,value.st_size,value.st_mtime_ns,value.st_ctime_ns)
        if stat_identity(before)!=stat_identity(after):raise RuntimeError("Matched source record changed while reading metadata")
        def invalid_constant(token):raise ValueError("Nonfinite matched source metadata: "+token)
        record=json.loads(raw,parse_constant=invalid_constant,
            parse_int=lambda token:-0.0 if token=="-0" else int(token))
        binding=record["native_binding"];field=record["candidate_field"]
        service=record["service_configuration"];source_identity=record["source_identity"]
        expected_cells=512*scale*scale;expected_root_cells=[32*scale,16*scale,1]
        spacing=binding["stored_nominal_spacing"]
        if (record.get("schema")!="arch-materialized-native-source-1"
            or record.get("source_only_checked") is not True or record.get("physical_qualified") is not False
            or field.get("physical_qualified") is not False or binding.get("dimension")!=2
            or record.get("scope")!="materialized_source_only"
            or record.get("root_bounds")!=[0.,1.,-.5,.5]
            or binding.get("origin",[])[:2]!=[0.,-.5] or binding.get("root_upper",[])[:2]!=[1.,.5]
            or binding.get("periodic")!=[False,False,False]
            or record["field_call"].get("invoke_failed") is not False
            or record["field_call"].get("field_solve_failed") is not False
            or record["field_call"].get("actual_candidate_observed") is not True
            or source_identity.get("time")!=0. or field.get("source_generation")!=source_identity.get("generation")
            or service.get("origin")!="actual-SelfGravity-constructor-copy" or service.get("boundary")!="isolated"
            or service.get("relative_tolerance")!=1.e-10 or service.get("absolute_tolerance")!=0.
            or service.get("max_cycles")!=200
            or binding.get("root_cells")!=expected_root_cells
            or len(binding["patches"])!=2*scale*scale
            or len(record["source"]["leaves"])!=expected_cells or len(field["cell_values"])!=expected_cells
            or any(type(leaf.get("density")) not in (int,float) or leaf["density"]!=1.
                or leaf.get("level")!=0 for leaf in record["source"]["leaves"])
            or len(spacing)!=3 or any(type(x) not in (int,float) or not math.isfinite(x) or x<=0 for x in spacing)
            or spacing[0]!=1./expected_root_cells[0] or spacing[1]!=1./expected_root_cells[1]):
            raise RuntimeError("Matched export differs from the frozen actual uniform root layout")
        # The actual receipt supplies all observed geometry/identity. The
        # expected dyadic layout above authenticates this maintainer scenario;
        # it never constructs source edges or observation positions.
        summary["matched_resolution"].update(actualBindingObserved=True,
            actualRootCells=binding["root_cells"],actualRootOrigin=binding["origin"],actualRootUpper=binding["root_upper"],
            actualStoredNominalSpacing=spacing,radialSpacing=spacing[0],axialSpacing=spacing[1],hMax=max(spacing[:2]),
            hDefinition="maximum actual stored r/z spacing of this uniform canonical root layout",
            sourceIdentity=record["source_identity"],sourceGeneration=field["source_generation"],fieldGeneration=field["field_generation"],
            actualAmrLeafLevel=0,actualRootPatchCount=len(binding["patches"]),
            actualServiceConfiguration=record["service_configuration"],materializedRecordSha256=hashlib.sha256(raw).hexdigest(),
            actualRecordStat=dict(device=after.st_dev,inode=after.st_ino,size=after.st_size,mtimeNs=after.st_mtime_ns,ctimeNs=after.st_ctime_ns))
(out/"result.json").write_text(json.dumps(summary,indent=2)+"\n")
print(json.dumps(summary));raise SystemExit(result.returncode)
