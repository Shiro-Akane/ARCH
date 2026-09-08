#!/usr/bin/env python3
"""Reject CUDA architecture shortcuts that conflict with the frozen mainline."""

import argparse
import pathlib
import re
import sys


_CUDA_DEVICE_OBJECT_SOURCES = {
    "arch_cuda_backend_burn_ideal":
        "src/cuda/runtime/burn/routes/cudabackendburnideal.cu",
    "arch_cuda_backend_burn_helm":
        "src/cuda/runtime/burn/routes/cudabackendburnhelm.cu",
    "arch_cuda_backend_burn_tabular3d":
        "src/cuda/runtime/burn/routes/cudabackendburntabular3d.cu",
    "arch_cuda_backend_burn_tabular3d_aprox13":
        "src/cuda/runtime/burn/routes/cudabackendburntabular3daprox13.cu",
    "arch_cuda_backend_burn_tabular3d_aprox19":
        "src/cuda/runtime/burn/routes/cudabackendburntabular3daprox19.cu",
    "arch_cuda_backend_burn_tabular3d_aprox21":
        "src/cuda/runtime/burn/routes/cudabackendburntabular3daprox21.cu",
    "arch_cuda_backend_burn_tabular3d_iso7":
        "src/cuda/runtime/burn/routes/cudabackendburntabular3diso7.cu",
    "arch_cuda_backend_burn_tabular4d":
        "src/cuda/runtime/burn/routes/cudabackendburntabular4d.cu",
    "arch_cuda_backend_burn_tabular4d_aprox13":
        "src/cuda/runtime/burn/routes/cudabackendburntabular4daprox13.cu",
    "arch_cuda_backend_burn_tabular4d_aprox19":
        "src/cuda/runtime/burn/routes/cudabackendburntabular4daprox19.cu",
    "arch_cuda_backend_burn_tabular4d_aprox21":
        "src/cuda/runtime/burn/routes/cudabackendburntabular4daprox21.cu",
    "arch_cuda_backend_burn_tabular4d_iso7":
        "src/cuda/runtime/burn/routes/cudabackendburntabular4diso7.cu",
    "arch_cuda_backend_hydro_ideal":
        "src/cuda/runtime/hydro/cudabackendhydroideal.cu",
    "arch_cuda_backend_hydro_helm":
        "src/cuda/runtime/hydro/cudabackendhydrohelm.cu",
    "arch_cuda_backend_hydro_tabular3":
        "src/cuda/runtime/hydro/cudabackendhydrotabular3.cu",
    "arch_cuda_backend_hydro_tabular4":
        "src/cuda/runtime/hydro/cudabackendhydrotabular4.cu",
    "arch_cuda_backend_diffusion":
        "src/cuda/runtime/diffusion/cudabackenddiffusion.cu",
    "arch_cuda_backend_exchange":
        "src/cuda/runtime/amr/cudabackendexchange.cu",
    "arch_cuda_backend_amr_flux":
        "src/cuda/runtime/amr/cudabackendamrflux.cu",
    "arch_cuda_backend_amr_indicators":
        "src/cuda/amr/refinementindicators.cu",
    "arch_cuda_backend_amr_migration":
        "src/cuda/amr/regridmigration.cu",
    "arch_cuda_backend_grid_metrics":
        "src/cuda/common/gridmetricscache.cu",
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
        "src/cuda/runtime/control/cudabackendresources.cpp", "-g1"),
    "arch_cuda_backend_core": (
        "src/cuda/runtime/control/cudabackendcore.cpp", "-g1"),
    "arch_cuda_backend_factory": (
        "src/cuda/runtime/cudabackendfactory.cpp", "-g1"),
    "arch_cuda_backend_hydro_control": (
        "src/cuda/runtime/hydro/cudabackendhydrocontrol.cpp", "-g1"),
    "arch_cuda_backend_microphysics_control": (
        "src/cuda/runtime/control/cudabackendmicrophysicscontrol.cpp", "-g1"),
    "arch_cuda_backend_store": (
        "src/cuda/runtime/control/cudabackendstore.cpp", "-g1"),
    "arch_cuda_backend_indicators": (
        "src/cuda/runtime/amr/cudabackendindicators.cpp", "-g1"),
    "arch_cuda_backend_migration": (
        "src/cuda/runtime/amr/cudabackendmigration.cpp", "-g1"),
    "arch_cuda_backend_sparse_factory": (
        "src/cuda/runtime/burn/cudabackendburnsparsefactory.cpp", "-g1"),
}

# These executables link an already compiled production object, rather than
# compiling the production source a second time. Match both the test entrypoint
# and object; a target name alone does not grant production ownership.
_CUDA_FOCUSED_OBJECT_CONSUMERS = {
    "arch_cuda_regrid_migration": (
        "tests/cuda/test_cuda_regrid_migration.cu",
        "arch_cuda_backend_amr_migration"),
    "arch_cuda_refinement_indicators": (
        "tests/cuda/test_refinement_indicators.cpp",
        "arch_cuda_backend_amr_indicators"),
    "arch_cuda_grid_metrics_cache": (
        "tests/cuda/test_grid_metrics_cache.cu",
        "arch_cuda_backend_grid_metrics"),
}

_CUDA_FOCUSED_LINK_OBJECT_CONSUMERS = {
    "arch_cuda_burn_policy_parity": (
        ("tests/cuda/test_burn_policy_parity.cu", "src/core/filefingerprint.cpp"),
        ("arch_cuda_backend_eos_helm", "arch_cuda_backend_eos_species",
         "arch_cuda_backend_eos_utils")),
    "arch_cuda_burn_controller_parity": (
        ("tests/cuda/test_burn_controller_parity.cu",),
        ("arch_cuda_backend_eos_helm", "arch_cuda_backend_eos_species",
         "arch_cuda_backend_eos_utils")),
}

# Reviewed generated binding families. The source, target and generating
# template must all agree, and their objects may enter only arch_cuda_backend.
# This is deliberately not a general exception for dynamic CMake target names.
_CUDA_GENERATED_OBJECT_ROUTES = {
    "cmake/cudacustomdenseroute.cu.in": (
        "arch_cuda_burn_${custom_eos_tag}_${custom_id}",
        "${arch_custom_registry_dir}/burn_${custom_eos_tag}_${custom_id}.cu"),
    "cmake/templates/cudaburndenseroute.cu.in": (
        "arch_cuda_backend_burn_${eos_token}_${network_name}",
        "${cmake_current_binary_dir}/generated/cuda_dense_burn/"
        "${eos_token}_${network_name}.cu"),
    "cmake/templates/cudaburnsparseowner.cu.in": (
        "arch_cuda_backend_sparse_${network_name}_${eos_token}",
        "${cmake_current_binary_dir}/generated/cuda_sparse_burn/"
        "${network_name}_${eos_token}.cu"),
}

# These names describe resource ownership or runtime orchestration; they are not
# alternate homes for mathematical formulae.  Keep this list exact so a new
# Core/Adapter/Device source still fails closed until its role is reviewed.
_CUDA_FORMULA_FILENAME_EXCEPTIONS = frozenset({
    "src/cuda/runtime/control/cudabackendcore.cpp",
    "src/cuda/microphysics/helm_eos_device_owner.h",
    "src/cuda/microphysics/helm_eos_device_owner.cpp",
    "src/cuda/microphysics/tabular3_eos_device_owner.h",
    "src/cuda/microphysics/tabular3_eos_device_owner.cpp",
    "src/cuda/microphysics/tabular4_eos_device_owner.h",
    "src/cuda/microphysics/tabular4_eos_device_owner.cpp",
})

_CUDA_RUNTIME_FUNCTION_OWNERS = {
    "compute_hydro_dt": "src/cuda/runtime/hydro/cudabackendhydrocontrol.cpp",
    "execute_hydro_stage": "src/cuda/runtime/hydro/cudabackendhydrocontrol.cpp",
    "execute_physical_boundary":
        "src/cuda/runtime/hydro/cudabackendhydrocontrol.cpp",
    "compute_diffusion_dt":
        "src/cuda/runtime/control/cudabackendmicrophysicscontrol.cpp",
    "execute_diffusion_stage":
        "src/cuda/runtime/control/cudabackendmicrophysicscontrol.cpp",
    "execute_burn":
        "src/cuda/runtime/control/cudabackendmicrophysicscontrol.cpp",
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
        if (path.name == "CMakeLists.txt"
                or path.suffix.lower() in {
                    ".cu", ".cuh", ".cpp", ".h", ".hpp", ".inc", ".py", ".cmake"}
                or path.name.lower().endswith(".cu.in")):
            yield path


def _without_cpp_comments(content: str, *, strings: bool = False) -> str:
    """Keep executable tokens separate from comments/diagnostic literals.

    Preserve whitespace/newlines so stripping cannot concatenate identifiers.
    Raw string literals are handled before ordinary quoted strings.
    """
    pattern = (r'[rR]"([^ ()\\\t\r\n]{0,16})\(.*?\)\1"'
               r'|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\''
               r'|/\*.*?\*/|//[^\n]*')

    def replace(match):
        value = match.group()
        if not strings and not value.startswith(("//", "/*")):
            return value
        return re.sub(r"[^\n]", " ", value)

    return re.sub(pattern, replace, content, flags=re.DOTALL)


def _cmake_code(content: str) -> str:
    # CMake flags and source names often live in strings, so retain them while
    # removing line/bracket comments. A '#' inside a quoted argument is data.
    pattern = r'"(?:\\.|[^"\\])*"|#\[(=*)\[.*?\]\1\]|#[^\n]*'
    return re.sub(pattern, lambda m: m.group() if m.group().startswith('"')
                  else re.sub(r"[^\n]", " ", m.group()),
                  content, flags=re.DOTALL).lower()


def _cmake_arguments(arguments: str):
    return [token[1:-1] if token.startswith('"') and token.endswith('"')
            else token
            for token in re.findall(r'"(?:\\.|[^"\\])*"|[^\s]+', arguments)]


def _cmake_commands(code: str, names: str):
    # The audited source/target/helper commands have no nested argument lists.
    # Quoted diagnostic parentheses must not truncate a command.
    for match in re.finditer(
            r"\b(" + names + r')\s*\(((?:"(?:\\.|[^"\\])*"|[^)"])*?)\)',
            code, flags=re.DOTALL):
        yield match.group(1), _cmake_arguments(match.group(2))


def _canonical_source(source: str) -> str:
    return re.sub(r"^\$\{(?:cmake_current_source_dir|project_source_dir|"
                  r"cmake_source_dir)\}/", "", source).removeprefix("./")


def _generated_backend_objects(root: pathlib.Path, relative: str, code: str, violations):
    """Prove local configure -> compile -> archive ownership for each route.

    CMake is not executed by this source audit. Ambiguous/reassigned bindings
    fail closed instead of guessing their configure-time values.
    """
    commands = list(_cmake_commands(
        code, "set|configure_file|arch_configure_cuda_backend_object|"
        "add_library|add_executable|target_sources"))
    assignments = {}
    for name, arguments in commands:
        if name == "set" and len(arguments) >= 2:
            assignments.setdefault(arguments[0], []).append(arguments[1:])

    def assigned(variable):
        values = assignments.get(variable, [])
        return values[0][0] if len(values) == 1 and len(values[0]) == 1 else None

    generated = {}
    for name, arguments in commands:
        if name != "configure_file" or not arguments:
            continue
        template = arguments[0]
        if not template.endswith(".cu.in"):
            continue
        template = re.sub(
            r"^\$\{cmake_current_(?:function_)?list_dir\}/",
            pathlib.PurePosixPath(relative).parent.as_posix() + "/", template)
        template = _canonical_source(template).removeprefix("./")
        expected = _CUDA_GENERATED_OBJECT_ROUTES.get(template)
        output = (re.fullmatch(r"\$\{(\w+)\}", arguments[1])
                  if len(arguments) == 3 and arguments[2] == "@only" else None)
        helper_calls = [args for command, args in commands
                        if command == "arch_configure_cuda_backend_object"
                        and output and len(args) == 2
                        and args[1] == arguments[1]]
        target_variable = (re.fullmatch(r"\$\{(\w+)\}", helper_calls[0][0])
                           if len(helper_calls) == 1 else None)
        source = assigned(output.group(1)) if output else None
        if source and source.startswith("${route_dir}/"):
            directory = assigned("route_dir")
            source = source.replace("${route_dir}", directory, 1) if directory else None
        target = assigned(target_variable.group(1)) if target_variable else None
        template_exists = any(path.is_file() and path.relative_to(root).as_posix().lower() == template
                              for path in (root / "cmake").rglob("*.cu.in"))
        valid = expected is not None and (target, source) == expected and template_exists
        if valid:
            object_name = helper_calls[0][0]
            consumers = [args[0] for command, args in commands
                         if command in {"add_library", "add_executable", "target_sources"}
                         and args and f"$<target_objects:{object_name}>" in args]
            source_spellings = {arguments[1], assigned(output.group(1)), source}
            compilations = [(command, args) for command, args in commands
                            if command in {"arch_configure_cuda_backend_object", "add_library",
                                           "add_executable", "target_sources"}
                            and any(arg in source_spellings for arg in args[1:])]
            valid = (consumers == ["arch_cuda_backend"] and compilations == [
                ("arch_configure_cuda_backend_object", helper_calls[0])])
        if not valid:
            violations.append(
                f"generated CUDA binding must keep one canonical backend owner: {relative}")
        else:
            generated[object_name] = (arguments[1], source, target)
    return generated


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


def _is_pod_geometry_adapter(relative: str, content: str) -> bool:
    if relative != "src/cuda/hydro/GridGeometryAdapter.cuh":
        return False
    # Permit only the reviewed enum/field bridge, including the unsupported
    # geometry guard. Additional functions, arithmetic or remapped fields must
    # be reviewed; the word Adapter alone is never an exception.
    expected = """
#pragma once
#include "cuda/common/CudaCommon.cuh"
#include "grid/GridMetrics.h"
namespace arch::cuda {
ARCH_HOST_DEVICE inline GridMetrics::GeometryView make_grid_geometry_view(
    const DeviceGridView& grid) {
    const auto kind = grid.geometry == static_cast<int>(DeviceGeometry::Cartesian)
        ? GridMetrics::Geometry::Cartesian
        : grid.geometry == static_cast<int>(DeviceGeometry::Cylindrical)
            ? GridMetrics::Geometry::Cylindrical
            : grid.geometry == static_cast<int>(DeviceGeometry::Spherical)
                ? GridMetrics::Geometry::Spherical : GridMetrics::Geometry::Unsupported;
    return {kind, grid.dim, grid.ng, grid.stride_y, grid.stride_z,
            grid.total_size, grid.dx1, grid.dx2, grid.dx3,
            grid.x1_min, grid.x2_min, grid.x3_min};
}
}
"""
    normalize = lambda value: re.sub(r"\s+", "", _without_cpp_comments(value))
    return normalize(content) == normalize(expected)


def _is_generated_dispatch_delegate(relative: str, content: str) -> bool:
    """Generated translation units may bind types, not own numerical bodies."""
    dense_call = """
        if constexpr (BurnLimits::uses_compact_matrix(Network::ODE_NEQ))
            return visit_ode_route<Network>(plan, state, grid, workspace_storage,
                candidates, statuses, summary, burn_dt, eos, config, stream);
        else return cudaErrorInvalidValue;
    """
    bodies = {
        "cmake/cudacustomdenseroute.cu.in": [
            dense_call.replace("workspace_storage", "workspace").replace("burn_dt", "dt"),
            """return custom_dense_route<@custom_type@>(plan, state, grid, workspace,
                candidates, statuses, summary, dt, eos, config, stream);"""],
        "cmake/templates/cudaburndenseroute.cu.in": [
            dense_call,
            """using Network = @ARCH_DENSE_NETWORK_TYPE@;
            return launch_compact_registered_network<Network>(plan, state, grid,
                workspace_storage, candidates, statuses, summary, burn_dt, eos,
                config, stream);"""],
        "cmake/templates/cudaburnsparseowner.cu.in": [
            """using Network = @ARCH_SPARSE_NETWORK_TYPE@;
            return make_sparse_burn_owner_for_network<Network>(plan, eos, max_cells,
                stream);"""],
    }
    expected = bodies.get(relative.lower())
    if expected is None:
        return False
    code = _without_cpp_comments(content, strings=True)
    normalize = lambda value: re.sub(r"\s+", "", value)
    actual = re.findall(r"\{([^{}]*)\}", code)
    declarations = re.sub(r"\{[^{}]*\}", "{}", code)
    return ([normalize(body) for body in actual]
            == [normalize(body) for body in expected]
            and not re.search(r"\b(?:__global__|__device__|__host__)\b", code)
            and not re.search(r"(?<![<>=!])=(?!=)", declarations))


def audit_header_dependencies(root: pathlib.Path, sources):
    """Inspect local include edges using the already-read source snapshot.

    This is a lexical source-layer contract, not a replacement for compiler
    dependency records or a C++ preprocessor. External/generated headers are
    resolved by CMake/compiler tests; never invent edges for their basenames.
    """
    root = root.resolve()
    headers = {path.resolve(): content for path, content in sources
               if path.suffix.lower() in {".h", ".hpp", ".cuh", ".inc"}
               and path.resolve().is_relative_to(root / "src")}
    source_namespaces = {path.relative_to(root / "src").parts[0]
                         for path in headers
                         if len(path.relative_to(root / "src").parts) > 1}
    graph, unresolved = {}, []
    for path, content in headers.items():
        graph[path] = []
        for include in re.findall(r'^\s*#\s*include\s*"([^"\n]+)"',
                                  _without_cpp_comments(content), re.MULTILINE):
            candidates = ((path.parent / include).resolve(),
                          (root / "src" / include).resolve())
            for candidate in candidates:
                if candidate in headers:
                    graph[path].append(candidate)
                    break
            else:
                # Relative includes and paths in an existing source namespace
                # are source-owned. Bare generated names and unrelated SDK
                # prefixes still belong to the configured compiler's -I paths.
                relative = include.startswith(("./", "../"))
                project_path = "/" in include and include.split("/", 1)[0] in source_namespaces
                if (relative or project_path) and not any(
                        candidate.is_file() for candidate in candidates):
                    kind = "relative" if relative else "project"
                    unresolved.append(f"unresolved {kind} source include: "
                                      + path.relative_to(root).as_posix() + " -> " + include)
    violations, active, visited = unresolved, [], set()

    def visit(path):
        if path in active:
            cycle = active[active.index(path):] + [path]
            violations.append("local header include cycle: " + " -> ".join(
                node.relative_to(root).as_posix() for node in cycle))
            return
        if path in visited:
            return
        active.append(path)
        for dependency in graph[path]:
            visit(dependency)
        active.pop()
        visited.add(path)

    for path in sorted(graph):
        visit(path)
    def dependencies(relative):
        pending, reached = [root / "src" / relative], set()
        while pending:
            path = pending.pop()
            if path in reached:
                continue
            reached.add(path)
            pending.extend(graph.get(path, ()))
        return reached

    catalogue = root / "src/numerics/burnsolver/Networks.h"
    for relative in ("driver/DriverBurn.h", "driver/DriverBurnPolicy.h",
                     "numerics/burnsolver/ode_bd.h", "numerics/burnsolver/ode_be-nr.h",
                     "numerics/burnsolver/ode_ros4.h"):
        if catalogue in dependencies(relative):
            violations.append("generic burn mathematics must not include the network catalogue: " + relative)

    # Interface/storage declarations must not pull the numerical graph back
    # into every dispatch TU. A real owner may include complete EOS types;
    # declaration-only ABIs and shared cell policies may not import the owner.
    interfaces = ("cuda/runtime/burn/CudaBackendBurn.h", "cuda/runtime/burn/CudaBackendBurnSparse.h",
                  "cuda/runtime/hydro/CudaBackendHydro.h", "cuda/runtime/diffusion/CudaBackendDiffusion.h",
                  "cuda/common/CudaLaunchConfig.h")
    eos_bodies = {root / "src/physics/eos" / name for name in
                  ("IdealGas.h", "HelmEos.h", "Tabular3DEOS.h", "Tabular4DEOS.h")}
    for relative in interfaces:
        if dependencies(relative) & eos_bodies:
            violations.append("CUDA declaration ABI must not import EOS implementations: " + relative)
    for relative in (*interfaces, "driver/DriverBurnPolicy.h", "cuda/microphysics/common.h"):
        forbidden = {root / "src" / name for name in
                     ("driver/DriverBurn.h", "core/RuntimeParams.h",
                      "physics/nse/nse_solver.h")}
        if dependencies(relative) & forbidden:
            violations.append("burn/launch declarations must not import host iteration, parsing or NSE: " + relative)
    for relative in ("cuda/common/DeviceAllocation.h", "cuda/runtime/burn/CudaBackendBurnSparseImpl.cuh"):
        if root / "src/cuda/runtime/control/CudaBackendInternal.h" in dependencies(relative):
            violations.append("typed sparse/allocation owner must not import the complete runtime layout: " + relative)
    for relative in (*interfaces, "numerics/diffusion/DiffusionTypes.h"):
        forbidden = {root / "src" / name for name in
                     ("grid/Grid.h", "numerics/diffusion/DiffFlux.h",
                      "numerics/integrator/TimeIntegratorHelper.h")}
        if dependencies(relative) & forbidden:
            violations.append("launch/types declarations must not import grid or numerical operators: " + relative)
    support = "physics/network/timmes_common/TimmesNetworkSupport.h"
    providers = {root / "src" / name for name in
                 ("core/RuntimeParams.h", "numerics/linalg/DenseWrap.h",
                  "numerics/linalg/SparseWrap.h")}
    if dependencies(support) & providers:
        violations.append("duck-typed network support must not import parsing or linear providers: " + support)
    return violations


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

    sources = [(path, path.read_text(encoding="utf-8", errors="ignore"))
               for path in _source_files(root)]
    violations.extend(audit_header_dependencies(root, sources))
    cmake_all = "\n".join(_cmake_code(content) for path, content in sources
                          if path.name == "CMakeLists.txt" or path.suffix == ".cmake")
    # A helper may be defined in the root and called by included functional
    # modules. Compile ownership is global even though registrations are local.
    cuda_source_owners = {}

    for path, content in sources:
        relative = path.relative_to(root).as_posix()
        lowered = relative.lower()
        content_lower = content.lower()
        semantic_code = _without_cpp_comments(content_lower, strings=True)
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
        fallback_scan = semantic_code
        if relative in {
            "src/driver/dispatch/BackendCapabilities.h",
            "src/driver/SolverDispatch.cpp",
        }:
            fallback_scan = re.sub(r"\bfallback_reason\b", "", fallback_scan)
        cuda_production = lowered.startswith("src/cuda/")
        if (cuda_production
                and not _is_pod_geometry_adapter(relative, content)
                and lowered not in _CUDA_FORMULA_FILENAME_EXCEPTIONS
                and any(token in lowered for token in ("core", "adapter", "_device"))):
            violations.append(f"formula-copy filename is forbidden: {relative}")
        if cuda_production and ("runner" in lowered or "int main(" in semantic_code):
            violations.append(f"second CUDA runner is forbidden: {relative}")
        stage_loop = re.search(r"(?:for|while)\s*\([^)]*\bstage\b|for_each_stage", semantic_code)
        stage_work = any(token in semantic_code for token in ("launch", "update", "advance", "kernel"))
        if cuda_production and ("rkcontroller" in lowered or "rklcontroller" in lowered or
                                (stage_loop and stage_work)):
            violations.append(f"complete CUDA RK/RKL controller is forbidden: {relative}")
        boundary_mapping = any(token in semantic_code for token in
                               ("outflow", "reflect", "periodic", "physical boundary", "boundary_type",
                                "x1l_boundary", "x1r_boundary", "grid boundary"))
        # Stripping diagnostics must not hide actual string-based rule dispatch.
        boundary_mapping |= bool(re.search(
            r'["\'](?:outflow|reflect|reflecting|periodic)["\']\s*(?:==|!=)|'
            r'(?:==|!=)\s*["\'](?:outflow|reflect|reflecting|periodic)["\']',
            _without_cpp_comments(content_lower)))
        if cuda_production and (("boundary" in lowered and "fill_boundary" in semantic_code) or boundary_mapping):
            violations.append(f"CUDA boundary-rule duplication is forbidden: {relative}")
        if any(name in lowered for name in ("main_cuda", "cudasimulation", "cudaoutput")):
            violations.append(f"legacy CUDA entrypoint is forbidden: {relative}")
        if lowered.startswith("src/") and ("fallback" in fallback_scan and
                ("cpu" in fallback_scan or cuda_production
                 or re.search(r"\bfallback_reason\b", fallback_scan))):
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
        if path.name.lower().endswith(".cu.in") and not _is_generated_dispatch_delegate(relative, content):
            violations.append(f"generated CUDA templates must remain shared-implementation delegates: {relative}")
        if path.name == "CMakeLists.txt" or path.suffix.lower() == ".cmake":
            cmake_code = _cmake_code(content)
            generated_objects = _generated_backend_objects(
                root, lowered, cmake_code, violations)
            if re.search(
                    r"(?<![\w-])(?:-ffast-math|-ofast|--use_fast_math|"
                    r"-fassociative-math|-funsafe-math-optimizations)(?![\w-])",
                    cmake_code):
                violations.append(
                    "shared mathematics forbids fast-math/reassociation")
            if re.search(
                    r"add_library\s*\(\s*arch_build_contract\s+interface\s*\)",
                    cmake_code):
                strict_contract = re.search(
                    r"target_compile_options\s*\(\s*arch_build_contract\s+"
                    r"interface\b([^)]*)\)", cmake_code, flags=re.DOTALL)
                required_flags = (
                    "compile_lang_and_id:cxx", "-fno-fast-math",
                    "-ffp-contract=off", "compile_lang_and_id:cuda",
                    "--fmad=false", "--ftz=false", "--prec-div=true", "--prec-sqrt=true",
                    "-xcompiler=-fno-fast-math,-ffp-contract=off")
                if strict_contract is None or any(
                        flag not in strict_contract.group(1)
                        for flag in required_flags):
                    violations.append(
                        "shared mathematics needs one strict Host/CUDA build contract")
                link_contract = re.search(
                    r"target_link_options\s*\(\s*arch_build_contract\s+"
                    r"interface\b([^)]*)\)", cmake_code, flags=re.DOTALL)
                if link_contract is None or any(
                        flag not in link_contract.group(1)
                        for flag in ("link_lang_and_id:cxx", "link_lang_and_id:cuda",
                                     "-fno-fast-math", "-ffp-contract=off")):
                    violations.append(
                        "shared mathematics needs strict final-link semantics")
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
            for command, arguments in _cmake_commands(
                    cmake_code, "add_library|add_executable|target_sources"):
                if not arguments:
                    continue
                owner = arguments[0]
                for object_target in re.findall(
                        r"\$<target_objects:([^>]+)>", " ".join(arguments)):
                    allowed_solver = (owner == "arch"
                                      and object_target == "arch_solver_dispatch")
                    allowed_backend = (owner == "arch_cuda_backend"
                                       and (object_target in allowed_backend_objects
                                            or object_target in generated_objects))
                    focused = _CUDA_FOCUSED_OBJECT_CONSUMERS.get(owner)
                    allowed_test = (focused is not None
                                    and command == "add_executable"
                                    and arguments[1:] == [focused[0],
                                        f"$<target_objects:{focused[1]}>"])
                    if not allowed_solver and not allowed_backend and not allowed_test:
                        violations.append(
                            "OBJECT files may enter only their canonical owner")
            for _, arguments in _cmake_commands(cmake_code, "target_link_libraries"):
                if not arguments or arguments[0] == "arch_cuda_backend":
                    continue
                objects = []
                for argument in arguments[1:]:
                    expression = re.fullmatch(r"\$<target_objects:([^>]+)>", argument)
                    if expression:
                        objects.append(expression.group(1))
                    elif argument in allowed_backend_objects or argument in generated_objects:
                        objects.append(argument)
                if objects:
                    owner = arguments[0]
                    focused = _CUDA_FOCUSED_LINK_OBJECT_CONSUMERS.get(owner)
                    declarations = [args[1:] for _, args in _cmake_commands(cmake_all, "add_executable")
                                    if args and args[0] == owner]
                    if (focused is None or tuple(objects) != focused[1]
                            or declarations != [list(focused[0])]):
                        violations.append(
                            f"OBJECT files may enter only their canonical owner: {owner}")
            if re.search(r"target_sources\s*\(\s*arch\b[^)]*(?:\.cu|cuda)", cmake_code, flags=re.DOTALL):
                violations.append("ARCH must not receive CUDA sources or CUDA variables")
            helper_calls = [args for _, args in _cmake_commands(
                cmake_code, "arch_configure_cuda_backend_object")]
            helper_definitions = re.findall(
                r"function\s*\(\s*arch_configure_cuda_backend_object\s+"
                r"target\s+source\s*\)(.*?)endfunction\s*\(\s*\)",
                cmake_all, flags=re.DOTALL)
            helper_body = helper_definitions[0] if len(helper_definitions) == 1 else ""
            canonical_object_helper = (
                len(re.findall(r"\badd_library\s*\(", helper_body)) == 1
                and bool(re.search(
                    r"add_library\s*\(\s*\$\{target\}\s+object\s+"
                    r"\$\{source\}\s*\)", helper_body)))
            valid_helper_calls = canonical_object_helper
            for call_arguments in helper_calls:
                if len(call_arguments) != 2:
                    valid_helper_calls = False
                    continue
                owner, source = call_arguments
                source = _canonical_source(source)
                if owner in generated_objects:
                    valid_helper_calls &= source == generated_objects[owner][0]
                    source, owner = generated_objects[owner][1:]
                elif source != _CUDA_DEVICE_OBJECT_SOURCES.get(owner):
                    valid_helper_calls = False
                if owner in cuda_source_owners.get(source, set()):
                    violations.append("a CUDA source may appear only once per target")
                cuda_source_owners.setdefault(source, set()).add(owner)
            if helper_calls and not valid_helper_calls:
                violations.append(
                    "CUDA OBJECT libraries must keep their canonical owners")
            host_helper_calls = [args for _, args in _cmake_commands(
                cmake_code, "arch_configure_cuda_host_object")]
            host_helper_definitions = re.findall(
                r"function\s*\(\s*arch_configure_cuda_host_object\s+"
                r"target\s+source\s+debug_level\s*\)(.*?)"
                r"endfunction\s*\(\s*\)",
                cmake_all, flags=re.DOTALL)
            host_helper_body = (host_helper_definitions[0]
                                if len(host_helper_definitions) == 1 else "")
            canonical_host_helper = (
                len(re.findall(r"\badd_library\s*\(", host_helper_body)) == 1
                and bool(re.search(
                    r"add_library\s*\(\s*\$\{target\}\s+object\s+"
                    r"\$\{source\}\s*\)", host_helper_body)))
            valid_host_helper_calls = canonical_host_helper
            for call_arguments in host_helper_calls:
                expected = _CUDA_HOST_OBJECT_SPECS.get(call_arguments[0]) \
                    if call_arguments else None
                if (len(call_arguments) != 3 or expected is None
                        or (_canonical_source(call_arguments[1]), call_arguments[2]) != expected):
                    valid_host_helper_calls = False
            if host_helper_calls and not valid_host_helper_calls:
                violations.append(
                    "CUDA Host OBJECT libraries must keep their canonical owners")
            for _, arguments in _cmake_commands(cmake_code, "add_library"):
                if len(arguments) < 3 or arguments[1] != "object":
                    continue
                owner = arguments[0]
                cuda_sources = [_canonical_source(arg) for arg in arguments[2:]
                                if arg.endswith(".cu")]
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
                cpp_sources = [_canonical_source(arg) for arg in arguments[2:]
                               if arg.endswith(".cpp")]
                if (owner in _CUDA_HOST_OBJECT_SPECS
                        and cpp_sources != [_CUDA_HOST_OBJECT_SPECS[owner][0]]):
                    violations.append(
                        "CUDA Host OBJECT library has the wrong source owner")
            for _, arguments in _cmake_commands(
                    cmake_code, "add_library|add_executable|target_sources"):
                if not arguments:
                    continue
                owner = arguments[0]
                cuda_sources = [_canonical_source(arg) for arg in arguments[1:]
                                if arg.endswith(".cu")]
                if len(cuda_sources) != len(set(cuda_sources)):
                    violations.append(
                        "a CUDA source may appear only once per target")
                for source in cuda_sources:
                    if owner in cuda_source_owners.get(source, set()):
                        violations.append("a CUDA source may appear only once per target")
                    cuda_source_owners.setdefault(source, set()).add(owner)
            if re.search(r"add_executable\s*\(\s*arch\b[^)]*\.cu", cmake_code, flags=re.DOTALL):
                violations.append("ARCH must not compile CUDA sources directly")
    if any(len(owners) > 1 for owners in cuda_source_owners.values()):
        violations.append("a CUDA source may not have multiple target owners")
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
