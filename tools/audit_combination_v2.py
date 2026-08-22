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
        fallback_scan = content_lower
        if relative == "src/driver/dispatch/BackendCapabilities.h":
            fallback_scan = re.sub(r"\bfallback_reason\b", "", fallback_scan)
        cuda_production = lowered.startswith("src/cuda/")
        if cuda_production and any(token in lowered for token in ("core", "adapter", "_device")):
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
        if cuda_production and any(identifier.lower() in content_lower or identifier.lower() in lowered for identifier in
                                   ("CudaAuthorityIntegrator", "HydroIntegratorPolicies", "HydroSolver",
                                    "launch_diffusion_rkl", "DiffusionSolver")):
            violations.append(f"historical complete controller is forbidden: {relative}")
        if path.name == "CMakeLists.txt":
            cmake_code = "\n".join(line for line in content_lower.splitlines()
                                    if not line.lstrip().startswith("#"))
            if re.search(r"file\s*\(\s*glob[^)]*\.cu", cmake_code, flags=re.DOTALL):
                violations.append("production CUDA source glob is forbidden")
            if "target_objects:cuda" in cmake_code or "target_objects:arch_cuda" in cmake_code:
                violations.append("CUDA OBJECT injection into ARCH is forbidden")
            object_injections = re.findall(r"\$<target_objects:([^>]+)>", cmake_code)
            if any(target != "arch_solver_dispatch" for target in object_injections):
                violations.append("ARCH may consume only arch_solver_dispatch OBJECT files")
            if re.search(r"target_sources\s*\(\s*arch\b[^)]*(?:\.cu|cuda)", cmake_code, flags=re.DOTALL):
                violations.append("ARCH must not receive CUDA sources or CUDA variables")
            if re.search(r"add_library\s*\([^)]*\bobject\b[^)]*(?:\.cu|cuda)", cmake_code, flags=re.DOTALL):
                violations.append("CUDA OBJECT libraries are forbidden")
            cu_paths = re.findall(r"[\w./-]+\.cu\b", cmake_code)
            if any(cu_paths.count(path) > 1 for path in set(cu_paths)):
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
