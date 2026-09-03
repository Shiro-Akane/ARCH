#!/usr/bin/env python3
"""Reject CUDA architecture shortcuts that conflict with the frozen mainline."""

import argparse
import pathlib
import re
import sys


_CUDA_DEVICE_OBJECT_SOURCES = {
    "arch_cuda_backend_burn_ideal":
        "src/cuda/runtime/cudabackendburnideal.cu",
    "arch_cuda_backend_burn_helm":
        "src/cuda/runtime/cudabackendburnhelm.cu",
    "arch_cuda_backend_burn_tabular3d":
        "src/cuda/runtime/cudabackendburntabular3d.cu",
    "arch_cuda_backend_burn_tabular3d_aprox13":
        "src/cuda/runtime/cudabackendburntabular3daprox13.cu",
    "arch_cuda_backend_burn_tabular3d_aprox19":
        "src/cuda/runtime/cudabackendburntabular3daprox19.cu",
    "arch_cuda_backend_burn_tabular3d_aprox21":
        "src/cuda/runtime/cudabackendburntabular3daprox21.cu",
    "arch_cuda_backend_burn_tabular3d_iso7":
        "src/cuda/runtime/cudabackendburntabular3diso7.cu",
    "arch_cuda_backend_burn_tabular4d":
        "src/cuda/runtime/cudabackendburntabular4d.cu",
    "arch_cuda_backend_burn_tabular4d_aprox13":
        "src/cuda/runtime/cudabackendburntabular4daprox13.cu",
    "arch_cuda_backend_burn_tabular4d_aprox19":
        "src/cuda/runtime/cudabackendburntabular4daprox19.cu",
    "arch_cuda_backend_burn_tabular4d_aprox21":
        "src/cuda/runtime/cudabackendburntabular4daprox21.cu",
    "arch_cuda_backend_burn_tabular4d_iso7":
        "src/cuda/runtime/cudabackendburntabular4diso7.cu",
    "arch_cuda_backend_hydro_ideal":
        "src/cuda/runtime/cudabackendhydroideal.cu",
    "arch_cuda_backend_hydro_helm":
        "src/cuda/runtime/cudabackendhydrohelm.cu",
    "arch_cuda_backend_hydro_tabular3":
        "src/cuda/runtime/cudabackendhydrotabular3.cu",
    "arch_cuda_backend_hydro_tabular4":
        "src/cuda/runtime/cudabackendhydrotabular4.cu",
    "arch_cuda_backend_diffusion":
        "src/cuda/runtime/cudabackenddiffusion.cu",
    "arch_cuda_backend_exchange":
        "src/cuda/runtime/cudabackendexchange.cu",
    "arch_cuda_backend_amr_flux":
        "src/cuda/runtime/cudabackendamrflux.cu",
}

_CUDA_HOST_OBJECT_SPECS = {
    "arch_cuda_backend_eos_utils": (
        "src/cuda/microphysics/device_eos_owner_utils.cpp", "-g0"),
    "arch_cuda_backend_eos_species": (
        "src/cuda/microphysics/device_species_owner.cpp", "-g0"),
    "arch_cuda_backend_eos_helm": (
        "src/cuda/microphysics/helm_eos_device_owner.cpp", "-g0"),
    "arch_cuda_backend_eos_tabular3": (
        "src/cuda/microphysics/tabular3_eos_device_owner.cpp", "-g0"),
    "arch_cuda_backend_eos_tabular4": (
        "src/cuda/microphysics/tabular4_eos_device_owner.cpp", "-g0"),
    "arch_cuda_backend_resources": (
        "src/cuda/runtime/cudabackendresources.cpp", "-g1"),
    "arch_cuda_backend_core": (
        "src/cuda/runtime/cudabackendcore.cpp", "-g1"),
    "arch_cuda_backend_factory": (
        "src/cuda/runtime/cudabackendfactory.cpp", "-g1"),
    "arch_cuda_backend_hydro_control": (
        "src/cuda/runtime/cudabackendhydrocontrol.cpp", "-g1"),
    "arch_cuda_backend_microphysics_control": (
        "src/cuda/runtime/cudabackendmicrophysicscontrol.cpp", "-g1"),
    "arch_cuda_backend_store": (
        "src/cuda/runtime/cudabackendstore.cpp", "-g1"),
}

# These names describe resource ownership or runtime orchestration; they are not
# alternate homes for mathematical formulae.  Keep this list exact so a new
# Core/Adapter/Device source still fails closed until its role is reviewed.
_CUDA_FORMULA_FILENAME_EXCEPTIONS = frozenset({
    "src/cuda/runtime/cudabackendcore.cpp",
    "src/cuda/microphysics/helm_eos_device_owner.h",
    "src/cuda/microphysics/helm_eos_device_owner.cpp",
    "src/cuda/microphysics/tabular3_eos_device_owner.h",
    "src/cuda/microphysics/tabular3_eos_device_owner.cpp",
    "src/cuda/microphysics/tabular4_eos_device_owner.h",
    "src/cuda/microphysics/tabular4_eos_device_owner.cpp",
})

_CUDA_RUNTIME_FUNCTION_OWNERS = {
    "compute_hydro_dt": "src/cuda/runtime/cudabackendhydrocontrol.cpp",
    "execute_hydro_stage": "src/cuda/runtime/cudabackendhydrocontrol.cpp",
    "execute_physical_boundary":
        "src/cuda/runtime/cudabackendhydrocontrol.cpp",
    "compute_diffusion_dt":
        "src/cuda/runtime/cudabackendmicrophysicscontrol.cpp",
    "execute_diffusion_stage":
        "src/cuda/runtime/cudabackendmicrophysicscontrol.cpp",
    "execute_burn":
        "src/cuda/runtime/cudabackendmicrophysicscontrol.cpp",
}

_CUDA_SYNCHRONIZED_COUNTER_FUNCTIONS = frozenset({
    "compute_hydro_dt",
    "execute_hydro_stage",
    "compute_diffusion_dt",
    "execute_diffusion_stage",
    "execute_burn",
})

_CUDA_COMPLETION_LAUNCHES = {
    "compute_hydro_dt": r"\blaunch_cuda_backend_hydro_dt\s*\(",
    "execute_hydro_stage": r"\blaunch_cuda_backend_hydro_stage\s*\(",
    "compute_diffusion_dt": r"\blaunch_cuda_backend_diffusion_dt\s*\(",
    "execute_diffusion_stage":
        r"\blaunch_cuda_backend_diffusion_stage\s*\(",
    "execute_burn": r"\blaunch_cuda_burn_route\s*\(",
}


def _source_files(root: pathlib.Path):
    ignored = {".git", "build", "output", ".superpowers"}
    for path in root.rglob("*"):
        if not path.is_file() or any(part in ignored for part in path.parts):
            continue
        if path.name == "CMakeLists.txt" or path.suffix.lower() in {".cu", ".cuh", ".cpp", ".h", ".hpp", ".py"}:
            yield path


def _function_body(content: str, qualified_name: str):
    """Return one C++ definition body, excluding braces, or None."""
    match = re.search(r"\b" + re.escape(qualified_name) + r"\s*\(", content)
    if match is None:
        return None
    opening = content.find("{", match.end())
    if opening < 0:
        return None

    depth = 0
    index = opening
    quote = None
    while index < len(content):
        char = content[index]
        following = content[index + 1] if index + 1 < len(content) else ""
        if quote is not None:
            if char == "\\":
                index += 2
                continue
            if char == quote:
                quote = None
            index += 1
            continue
        if char in {'"', "'"}:
            quote = char
            index += 1
            continue
        if char == "/" and following == "/":
            newline = content.find("\n", index + 2)
            index = len(content) if newline < 0 else newline + 1
            continue
        if char == "/" and following == "*":
            closing = content.find("*/", index + 2)
            index = len(content) if closing < 0 else closing + 2
            continue
        if char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return content[opening + 1:index]
        index += 1
    return None


def _matches_in_order(content: str, *patterns: str) -> bool:
    position = 0
    for pattern in patterns:
        match = re.search(pattern, content[position:], flags=re.DOTALL)
        if match is None:
            return False
        position += match.end()
    return True


def _launch_quiesces_before_kernel_count(
        body: str, launch_pattern: str) -> bool:
    return _matches_in_order(
        body,
        launch_pattern,
        r"\bquiesce\s*\(\s*\)\s*;",
        r"impl_->runtime_counters\.kernel_count\s*\+=")


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
                and lowered not in _CUDA_FORMULA_FILENAME_EXCEPTIONS
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
        for function_name, owner in _CUDA_RUNTIME_FUNCTION_OWNERS.items():
            body = (_function_body(content, f"CudaBackend::{function_name}")
                    if lowered.startswith("src/cuda/runtime/")
                    and path.suffix.lower() in {".cpp", ".cu"}
                    else None)
            if body is None:
                continue
            if lowered != owner:
                violations.append(
                    f"CUDA runtime function has the wrong functional owner: "
                    f"{function_name} in {relative}")
                continue
            if (function_name in _CUDA_SYNCHRONIZED_COUNTER_FUNCTIONS
                    and not _launch_quiesces_before_kernel_count(
                        body, _CUDA_COMPLETION_LAUNCHES[function_name])):
                violations.append(
                    f"CUDA bounded work must quiesce before completion: "
                    f"{function_name}")
            if (function_name == "execute_physical_boundary"
                    and not _matches_in_order(
                        body,
                        r"\blaunch_cuda_backend_boundary_plan\s*\(",
                        r"\bquiesce\s*\(\s*\)\s*;",
                        r"for\s*\(\s*const\s+auto\s*&\s*phase\s*:\s*"
                        r"block\.boundary\.phases\s*\)")):
                violations.append(
                    "CUDA boundary completion must follow stream quiescence")
            if (function_name == "execute_burn"
                    and not re.search(
                        r"\bblock\.burn_workspace_storage\.get\(\)\s*,\s*"
                        r"block\.burn_candidates\.get\(\)\s*,\s*"
                        r"block\.burn_statuses\.get\(\)\s*,\s*"
                        r"block\.burn_summary\.get\(\)",
                        body, flags=re.DOTALL)):
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
            allowed_backend_objects = (
                set(_CUDA_DEVICE_OBJECT_SOURCES)
                | set(_CUDA_HOST_OBJECT_SPECS))
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
                        or call_arguments[0] not in _CUDA_DEVICE_OBJECT_SOURCES
                        or call_arguments[1]
                            != _CUDA_DEVICE_OBJECT_SOURCES[call_arguments[0]]):
                    canonical_object_helper = False
            if helper_calls and not canonical_object_helper:
                violations.append(
                    "CUDA OBJECT libraries must keep their canonical owners")
            host_helper_calls = re.findall(
                r"^\s*arch_configure_cuda_host_object\s*\(([^)]*)\)",
                cmake_code, flags=re.MULTILINE)
            host_helper_definition = re.search(
                r"function\s*\(\s*arch_configure_cuda_host_object\s+"
                r"target\s+source\s+debug_level\s*\)(.*?)"
                r"endfunction\s*\(\s*\)",
                cmake_code, flags=re.DOTALL)
            host_helper_body = (host_helper_definition.group(1)
                                if host_helper_definition else "")
            canonical_host_helper = (bool(host_helper_calls) and
                len(re.findall(r"\badd_library\s*\(", host_helper_body)) == 1
                and bool(re.search(
                    r"add_library\s*\(\s*\$\{target\}\s+object\s+"
                    r"\$\{source\}\s*\)", host_helper_body)))
            for call in host_helper_calls:
                call_arguments = call.split()
                expected = _CUDA_HOST_OBJECT_SPECS.get(call_arguments[0]) \
                    if call_arguments else None
                if (len(call_arguments) != 3 or expected is None
                        or tuple(call_arguments[1:]) != expected):
                    canonical_host_helper = False
            if host_helper_calls and not canonical_host_helper:
                violations.append(
                    "CUDA Host OBJECT libraries must keep their canonical owners")
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
                                     and (canonical_object_helper
                                          or canonical_host_helper))
                if not helper_definition:
                    if owner in _CUDA_DEVICE_OBJECT_SOURCES:
                        if cuda_sources != [
                                _CUDA_DEVICE_OBJECT_SOURCES[owner]]:
                            violations.append(
                                "CUDA OBJECT library has the wrong source owner")
                    elif cuda_sources:
                        violations.append("CUDA OBJECT libraries are forbidden")
                cpp_sources = re.findall(
                    r"[\w./-]+\.cpp\b", command.group(1))
                if (owner in _CUDA_HOST_OBJECT_SPECS
                        and cpp_sources != [_CUDA_HOST_OBJECT_SPECS[owner][0]]):
                    violations.append(
                        "CUDA Host OBJECT library has the wrong source owner")
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
