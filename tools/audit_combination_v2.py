#!/usr/bin/env python3
"""Reject CUDA architecture shortcuts that conflict with the frozen mainline."""

import argparse
import pathlib
import re
import sys


def _source_files(root: pathlib.Path):
    ignored = {".git", "build", "output", ".superpowers"}
    for path in root.rglob("*"):
        if not path.is_file() or any(part in ignored for part in path.parts):
            continue
        if path.name == "CMakeLists.txt" or path.suffix.lower() in {".cu", ".cuh", ".cpp", ".h", ".hpp", ".py"}:
            yield path


def _is_include_only_diagnostic_adapter(relative: str, content: str) -> bool:
    if relative != "src/cuda/common/DiffusionConfigViewAdapter.h":
        return False

    without_comments = re.sub(r"/\*.*?\*/", "", content, flags=re.DOTALL)
    without_comments = re.sub(r"//.*", "", without_comments)
    lines = [line.strip() for line in without_comments.splitlines()
             if line.strip()]
    include = '#include "numerics/diffusion/DiffFlux.h"'
    system_header = "#pragma GCC system_header"
    compiler_guard = "#if defined(__GNUC__) || defined(__clang__)"
    allowed = {
        "#pragma once",
        compiler_guard,
        system_header,
        "#endif",
        include,
    }
    if any(line not in allowed for line in lines):
        return False
    guarded = compiler_guard in lines or "#endif" in lines
    return (lines.count("#pragma once") == 1
            and lines.count(system_header) == 1
            and lines.count(include) == 1
            and (not guarded
                 or (lines.count(compiler_guard) == 1
                     and lines.count("#endif") == 1)))


def audit_tree(root: pathlib.Path):
    """Return violations for a repository root; an empty list is a pass."""
    root = pathlib.Path(root)
    violations = []
    protected = {
        "src/physics/diffusionCoe/diffusion_math.hpp": "double vie = iec * zbar * ymas * cint;",
        "src/io/ConfigParser.h": "expects true or false",
        "src/core/RuntimeParams.h": "parser.GetBool",
        "src/driver/DriverControl.h": "1.0e-12",
        "src/main.cpp": "config.Get<std::string>(\"log_dir\", config.io.out_dir)",
        "src/physics/eos/eos_Utils.h": "get_isentropic_state_at_pressure_factor",
        "src/physics/eos/Tabular3DEOS.h": "eos_utils",
        "src/physics/eos/Tabular4DEOS.h": "eos_utils",
        "src/core/UserInterface.h": "ProblemHelper.h",
        "src/core/ProblemHelper.cpp": "eos_utils::get_isentropic_state_at_pressure_factor",
        "src/core/ProblemHelper.h": "GetRootCellWidth",
        "simulation/SmoothAdvection/SmoothAdvection.cpp": "ProblemHelper::GetRootCellWidth",
        "simulation/DiffusionMode/DiffusionMode.cpp": "ProblemHelper::GetRootCellWidth",
        "simulation/Cellular/Cellular.par": "EOS_toolkit/tables/helmholtz/helm_table.dat",
    }
    for relative, required in protected.items():
        path = root / relative
        if not path.is_file() or required not in path.read_text(encoding="utf-8", errors="ignore"):
            violations.append(f"protected mainline authority changed: {relative}")

    for path in _source_files(root):
        relative = path.relative_to(root).as_posix()
        lowered = relative.lower()
        content = path.read_text(encoding="utf-8", errors="ignore")
        content_lower = content.lower()
        e3_regression_markers = (
            "resolve_execution_plan_bypassed",
            "reparsed_time",
            "reparsed_hydro",
            "handwritten_network_mapping",
            "parse_ode_again",
            "parse_linear_again",
            "parse_diffusion_again",
            "parse_gravity_again",
            "probe_after_backend_construction",
            "allocate_backend_before_resolution",
            "query_support_after_construction",
            "requested_omitted=",
            "resolved_omitted=",
            "runtime_omitted=",
            "mutate_plan_after_sidecar",
            "retained_block",
            "retained_pool_index",
            "upload_current_fluid_state",
            "allocate_and_initialize",
            "false && pointer_ != nullptr",
            "leaked_helm_owner",
            "cudabackend::~cudabackend_no_quiesce",
            "short_lived_tabular3_owner",
            "grid.cell_volume = block.grid.cell_volume.data",
            "derivative_nonnull_drift",
            "recomputed_boundary_sources",
            "recomputed_reflection_signs",
            "get_rkl_coeffs_drifted",
            "false && status != 0",
            "stage_loop_owned_by_cuda",
            "cuda_owned_rkl_loop",
        )
        if lowered.startswith("src/") and any(
                marker in content_lower for marker in e3_regression_markers):
            violations.append(f"E3 authority regression is forbidden: {relative}")
        if (relative == "src/cuda/runtime/CudaBackend.h"
                and "const simconfig& launch" in content_lower):
            violations.append("CUDA runtime must consume the frozen launch plan")
        if (relative == "src/core/ProblemHelper.cpp"
                and "ProblemInitializationContext" in content):
            bootstrap_eos_calls = content.count(
                "EOSDispatcher::dispatch_eos(config, specs")
            if (bootstrap_eos_calls != 2
                    or "EOSDispatcher::dispatch_eos(context.eos, config, specs"
                        not in content):
                violations.append(
                    "post-freeze EOS dispatch must consume ProblemInitializationContext")
        fallback_scan = content_lower
        if relative in {
            "src/driver/dispatch/BackendCapabilities.h",
            "src/driver/SolverDispatch.cpp",
        }:
            fallback_scan = re.sub(r"\bfallback_reason\b", "", fallback_scan)
        cuda_production = lowered.startswith("src/cuda/")
        diagnostic_include_adapter = _is_include_only_diagnostic_adapter(
            relative, content)
        if (cuda_production
                and not diagnostic_include_adapter
                and any(token in lowered for token in ("core", "adapter", "_device"))):
            violations.append(f"formula-copy filename is forbidden: {relative}")
        if cuda_production and ("runner" in lowered or "int main(" in content_lower):
            violations.append(f"second CUDA runner is forbidden: {relative}")
        stage_loop = re.search(r"(?:for|while)\s*\([^)]*\bstage\b|for_each_stage", content_lower)
        stage_work = any(token in content_lower for token in ("launch", "update", "advance", "kernel"))
        if cuda_production and ("rkcontroller" in lowered or "rklcontroller" in lowered or
                                (stage_loop and stage_work)):
            violations.append(f"complete CUDA RK/RKL controller is forbidden: {relative}")
        boundary_mapping = any(token in content_lower for token in
                               ("outflow", "reflect", "periodic", "physical boundary", "boundary_type",
                                "x1l_boundary", "x1r_boundary", "grid boundary"))
        if cuda_production and (("boundary" in lowered and "fill_boundary" in content_lower) or boundary_mapping):
            violations.append(f"CUDA boundary-rule duplication is forbidden: {relative}")
        if any(name in lowered for name in ("main_cuda", "cudasimulation", "cudaoutput")):
            violations.append(f"legacy CUDA entrypoint is forbidden: {relative}")
        if lowered.startswith("src/") and ("fallback" in fallback_scan and
                                             ("cpu" in fallback_scan or cuda_production)):
            violations.append(f"hidden CUDA fallback is forbidden: {relative}")
        bounded_stage_headers = {
            "src/cuda/hydro/HydroIntegratorPolicies.cuh",
            "src/cuda/diffusion/DiffusionSolver.cuh",
        }
        historical_content = re.sub(
            r"/\*.*?\*/|//[^\n]*", "", content_lower,
            flags=re.DOTALL)
        historical_content = re.sub(
            r"^\s*#include[^\n]*$", "", historical_content,
            flags=re.MULTILINE)
        historical_identifiers = (
            "CudaAuthorityIntegrator", "HydroIntegratorPolicies",
            "HydroSolver", "launch_diffusion_rkl", "DiffusionSolver")
        historical_in_path = relative not in bounded_stage_headers and any(
            identifier.lower() in lowered
            for identifier in historical_identifiers)
        historical_in_body = any(
            identifier.lower() in historical_content
            for identifier in historical_identifiers)
        if cuda_production and (historical_in_path or historical_in_body):
            violations.append(f"historical complete controller is forbidden: {relative}")
        if (relative == "src/cuda/hydro/HydroIntegratorPolicies.cuh"
                and "launch_bounded_hydro_stage" in content):
            required_hydro_lowering = (
                "clear_hydro_buffer(delta, stream)",
                "for (int direction = 0; direction < 3; ++direction)",
                "if (direction >= grid.dim) break;",
            )
            if any(required not in content
                   for required in required_hydro_lowering):
                violations.append(
                    "bounded CUDA Hydro lowering must clear and visit XYZ")
        if (relative == "src/cuda/runtime/CudaBackend.cu"
                and "execute_physical_boundary" in content
                and not re.search(
                    r"quiesce\(\)\s*;\s*for\s*"
                    r"\(const auto& phase : (?:impl_->boundary|block\.boundary)"
                    r"\.phases\)",
                    content)):
            violations.append(
                "CUDA boundary completion must follow stream quiescence")
        if (relative == "src/cuda/runtime/CudaBackend.cu"
                and "execute_hydro_stage" in content):
            synchronized_counter_updates = re.findall(
                r"quiesce\(\)\s*;\s*"
                r"impl_->runtime_counters\.kernel_count\s*\+=", content)
            if len(synchronized_counter_updates) < 5:
                violations.append(
                    "CUDA bounded work must quiesce before completion")
        if (relative == "src/cuda/runtime/CudaBackend.cu"
                and "burn_candidates" in content
                and not re.search(
                    r"(?:impl_->|block\.)burn_workspaces\.get\(\)\s*,\s*"
                    r"(?:impl_->|block\.)burn_candidates\.get\(\)", content)):
            violations.append(
                "CUDA burn routes must consume the caller workspace")
        if cuda_production and "without_failed_nse_continuation" in content:
            violations.append(
                "CUDA burn routes must preserve failed-NSE continuation")
        if (lowered.startswith("src/driver/")
                and "combine_full_host_state_minimum" in content):
            violations.append(
                "CUDA burn limiter must consume the compact candidate")
        if path.name == "CMakeLists.txt":
            cmake_code = "\n".join(line for line in content_lower.splitlines()
                                    if not line.lstrip().startswith("#"))
            if re.search(r"\bif\s*\(\s*true\s*\)", cmake_code):
                violations.append("CMake feature guards must not be unconditional")
            if re.search(
                    r"if\s*\(\s*build_testing\s*\)\s*"
                    r"add_library\s*\(\s*arch_cuda_backend\b",
                    cmake_code, flags=re.DOTALL):
                violations.append(
                    "production CUDA backend must exist when testing is off")
            if re.search(
                    r"target_sources\s*\(\s*arch\b[^)]*runtimeprobe\.cpp",
                    cmake_code, flags=re.DOTALL):
                violations.append("RuntimeProbe.cpp must have one target owner")
            if re.search(r"file\s*\(\s*glob[^)]*\.cu", cmake_code, flags=re.DOTALL):
                violations.append("production CUDA source glob is forbidden")
            allowed_backend_objects = {
                "arch_cuda_backend_burn_ideal",
                "arch_cuda_backend_burn_helm",
                "arch_cuda_backend_burn_tabular3d",
                "arch_cuda_backend_burn_tabular4d",
                "arch_cuda_backend_hydro",
                "arch_cuda_backend_diffusion",
                "arch_cuda_backend_exchange",
            }
            for command in re.finditer(
                    r"\b(?:add_library|add_executable|target_sources)\s*"
                    r"\(([^)]*)\)", cmake_code, flags=re.DOTALL):
                arguments = command.group(1).split()
                if not arguments:
                    continue
                owner = arguments[0]
                for object_target in re.findall(
                        r"\$<target_objects:([^>]+)>", command.group(1)):
                    allowed_solver = (owner == "arch"
                                      and object_target == "arch_solver_dispatch")
                    allowed_backend = (owner == "arch_cuda_backend"
                                       and object_target in allowed_backend_objects)
                    if not allowed_solver and not allowed_backend:
                        violations.append(
                            "OBJECT files may enter only their canonical owner")
            if re.search(r"target_sources\s*\(\s*arch\b[^)]*(?:\.cu|cuda)", cmake_code, flags=re.DOTALL):
                violations.append("ARCH must not receive CUDA sources or CUDA variables")
            allowed_object_sources = {
                "arch_cuda_backend_burn_ideal":
                    "src/cuda/runtime/cudabackendburnideal.cu",
                "arch_cuda_backend_burn_helm":
                    "src/cuda/runtime/cudabackendburnhelm.cu",
                "arch_cuda_backend_burn_tabular3d":
                    "src/cuda/runtime/cudabackendburntabular3d.cu",
                "arch_cuda_backend_burn_tabular4d":
                    "src/cuda/runtime/cudabackendburntabular4d.cu",
                "arch_cuda_backend_hydro":
                    "src/cuda/runtime/cudabackendhydro.cu",
                "arch_cuda_backend_diffusion":
                    "src/cuda/runtime/cudabackenddiffusion.cu",
                "arch_cuda_backend_exchange":
                    "src/cuda/runtime/cudabackendexchange.cu",
            }
            helper_calls = re.findall(
                r"^\s*arch_configure_cuda_backend_object\s*\(([^)]*)\)",
                cmake_code, flags=re.MULTILINE)
            helper_definition = re.search(
                r"function\s*\(\s*arch_configure_cuda_backend_object\s+"
                r"target\s+source\s*\)(.*?)endfunction\s*\(\s*\)",
                cmake_code, flags=re.DOTALL)
            helper_body = helper_definition.group(1) if helper_definition else ""
            canonical_object_helper = (bool(helper_calls)
                and len(re.findall(r"\badd_library\s*\(", helper_body)) == 1
                and bool(re.search(
                    r"add_library\s*\(\s*\$\{target\}\s+object\s+"
                    r"\$\{source\}\s*\)", helper_body)))
            for call in helper_calls:
                call_arguments = call.split()
                if (len(call_arguments) != 2
                        or call_arguments[0] not in allowed_object_sources
                        or call_arguments[1]
                            != allowed_object_sources[call_arguments[0]]):
                    canonical_object_helper = False
            for command in re.finditer(
                    r"add_library\s*\(([^)]*)\)", cmake_code,
                    flags=re.DOTALL):
                arguments = command.group(1).split()
                if len(arguments) < 3 or arguments[1] != "object":
                    continue
                owner = arguments[0]
                cuda_sources = re.findall(
                    r"[\w./-]+\.cu\b", command.group(1))
                helper_definition = (owner == "${target}"
                                     and arguments[2:] == ["${source}"]
                                     and canonical_object_helper)
                if (not helper_definition
                        and (owner not in allowed_object_sources
                        or cuda_sources != [allowed_object_sources[owner]])):
                    violations.append("CUDA OBJECT libraries are forbidden")
            cuda_source_owners = {}
            for command in re.finditer(
                    r"\b(?:add_library|add_executable|target_sources)\s*"
                    r"\(([^)]*)\)", cmake_code, flags=re.DOTALL):
                arguments = command.group(1).split()
                if not arguments:
                    continue
                owner = arguments[0]
                cuda_sources = re.findall(
                    r"[\w./-]+\.cu\b", command.group(1))
                if len(cuda_sources) != len(set(cuda_sources)):
                    violations.append(
                        "a CUDA source may appear only once per target")
                for source in cuda_sources:
                    cuda_source_owners.setdefault(source, set()).add(owner)
            if any(len(owners) > 1
                   for owners in cuda_source_owners.values()):
                violations.append("a CUDA source may not have multiple target owners")
            if re.search(r"add_executable\s*\(\s*arch\b[^)]*\.cu", cmake_code, flags=re.DOTALL):
                violations.append("ARCH must not compile CUDA sources directly")
    return violations


def main(argv=None):
    parser = argparse.ArgumentParser()
    parser.add_argument("root", nargs="?", default=".", type=pathlib.Path)
    arguments = parser.parse_args(argv)
    violations = audit_tree(arguments.root.resolve())
    for violation in violations:
        print(f"audit: {violation}")
    return 1 if violations else 0


if __name__ == "__main__":
    sys.exit(main())
