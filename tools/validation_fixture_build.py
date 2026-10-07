"""Build one private CPU fixture with an existing CTest link owner.

The Runtime-only owner keeps concrete policy instantiations outside private
fixtures. Legal weak template definitions alone prove neither stale objects nor
an ODR defect. This helper keeps configured LTO/FP flags and library order;
it does not configure/rebuild production, copy ARCH, or qualify scientific RZ.
"""
from __future__ import annotations

import json
import os
from pathlib import Path
import re
import shlex
import signal
import subprocess
import tempfile
from typing import Any

import validation_provenance as provenance


TARGET = "arch_gravity_stage_contract"
ORIGINAL_FIXTURES = (
    "tests/host/gravity/test_gravity_stage_contract.cpp",
    "tests/host/driver/test_host_hydro_transaction.cpp",
    "tests/host/driver/test_rz_runtime_boundary.cpp",
    "tests/host/driver/test_rz_runtime_external.cpp",
)
LINK_PRODUCTION_SOURCES = (
    "src/amr/elliptic/EllipticMeshAdapter.cpp",
    "src/driver/stages/GravityStage.cpp",
)
# Exact current CMake owner: test TUs are replaced, real production TUs retained.
ORIGINAL_FIXTURE_OBJECTS = tuple(f"CMakeFiles/{TARGET}.dir/{name}.o" for name in ORIGINAL_FIXTURES)
LINK_PRODUCTION_OBJECTS = tuple(f"CMakeFiles/{TARGET}.dir/{name}.o" for name in LINK_PRODUCTION_SOURCES)
EMBEDDED_FIXTURE_DEFINE = "-DARCH_RZ_RUNTIME_CONTRACT_EMBEDDED=1"
RUNTIME_SOURCES = ("src/driver/runtime/DriverRuntime.cpp",
    "src/driver/runtime/DriverBoundary.cpp", "src/driver/runtime/DriverBoundaryDiagnostics.cpp",
    "src/physics/boundary/PhysicalBoundaryHandler.cpp", "src/driver/runtime/DriverRegrid.cpp")
REUSABLE_SOURCES = {name: "ARCH" for name in (
    "src/core/problem/ProblemHelper.cpp", "src/physics/eos/eosdispatch.cpp",
    "src/core/files/FileFingerprint.cpp", "src/physics/eos/sources/TabularBaryonSource.cpp",
    "src/physics/eos/sources/TabularCompletion.cpp", "src/physics/eos/sources/Tabular3DEOS.cpp",
    "src/physics/eos/sources/Tabular4DEOS.cpp", "src/io/chk/ChkIO.cpp",
    "src/io/chk/CheckpointCompatibility.cpp", "src/io/hdf5/HDF5Writer.cpp",
    "src/io/plot/PlotIO.cpp", "src/core/files/BuildIdentity.cpp",
    "src/core/config/ConfigurationIdentity.cpp")}
REUSABLE_SOURCES["src/driver/io/DriverIO.cpp"] = "arch_solver_dispatch"
IO_FIXTURES = {"tests/host/io/test_driver_checkpoint_geometry.cpp",
               "tests/host/driver/test_rz_checkpoint_continuation.cpp"}
CONTROL_TOKENS = {"&&", ";", "|", ">", "<", "||", "&"}


def command_tokens(command: str) -> list[str]:
    """Parse trusted CMake argv, accepting only its exact outer link scaffold."""
    tokens = shlex.split(command)
    if tokens[:2] == [":", "&&"]:
        tokens = tokens[2:]
    if tokens[-2:] == ["&&", ":"]:
        tokens = tokens[:-2]
    if not tokens or any(token in CONTROL_TOKENS for token in tokens):
        raise RuntimeError("unsupported CMake command scaffolding")
    return tokens


def unique_entry(entries: list[dict[str, Any]], source: Path,
                 owner: str | None = None) -> dict[str, Any]:
    """Require one real source/target compile entry rather than guessing by name."""
    prefix = f"CMakeFiles/{owner}.dir/" if owner else None
    matches = [entry for entry in entries if Path(entry["file"]).resolve() == source
               and (prefix is None or prefix in Path(entry.get("output", "")).as_posix())]
    if len(matches) != 1:
        raise RuntimeError(f"missing or ambiguous compile entry: {source}")
    return matches[0]


def replace_option(tokens: list[str], option: str, value: str) -> list[str]:
    """Change exactly one source/output operand without changing configured flags."""
    result = list(tokens)
    if result.count(option) != 1:
        raise RuntimeError(f"missing or ambiguous command option: {option}")
    index = result.index(option)
    if index + 1 == len(result):
        raise RuntimeError(f"missing command option value: {option}")
    result[index + 1] = value
    return result


def fixture_compile_recipe(entry: dict[str, Any], production: dict[str, Any],
                           source: Path, output: Path, *,
                           recipe: str = "stage_contract") -> tuple[list[str], list[str]]:
    """Borrow the original CTest compile flags; retain declared production OpenMP.

    The old boundary runner used production OpenMP with two threads. Its CTest
    template may lack that compile option even though libgomp is linked. Copy
    only explicitly observed OpenMP controls, recording this reason; never
    invent a flag, disable LTO or change the shared floating-point semantics.
    """
    if recipe not in {"stage_contract", "production"}:
        raise RuntimeError("unknown fixture compile recipe")
    tokens = command_tokens(entry["command"] if recipe == "stage_contract" else production["command"])
    production_tokens = command_tokens(production["command"])
    if "-DARCH_CUDA_BUILD_ENABLED=0" not in tokens or \
            "-DARCH_CUDA_BUILD_ENABLED=0" not in production_tokens:
        raise RuntimeError("CPU compile contract required")
    # The target embeds Runtime test bodies, but a private fixture has its own
    # unchanged main. Strip only this known target-local switch; preserve all
    # strict-FP/LTO/hardware/physics controls, and reject unknown variants.
    embedded = [t for t in tokens if t.startswith("-DARCH_RZ_RUNTIME_CONTRACT_EMBEDDED")]
    if embedded not in ([], [EMBEDDED_FIXTURE_DEFINE]):
        raise RuntimeError("unexpected embedded Runtime fixture definition")
    if embedded:
        tokens.remove(EMBEDDED_FIXTURE_DEFINE)
    additions = []
    if recipe == "production":
        for token in command_tokens(entry["command"]):
            if token.startswith("-I") and Path(token[2:]).name == "tests" and token not in tokens:
                tokens.append(token)
    for token in ("-fopenmp", "-DARCH_OPENMP_ENABLED=1"):
        if token in production_tokens and token not in tokens:
            tokens.append(token)
            additions.append(token)
    tokens = replace_option(tokens, "-c", str(source))
    return replace_option(tokens, "-o", str(output)), additions


def fixture_link_recipe(commands: str, original_objects: list[str],
                        fixture_object: Path, executable: Path,
                        provider_objects: list[str] | None = None) -> list[str]:
    """Replace all exact test-owner objects; retain real production objects.

    The current owner has four test TUs plus two production TUs. Unknown,
    missing or duplicate objects remain errors; this is not arbitrary linking.
    Library/strict-FP/LTO order is preserved from the actual frozen CMake line.
    """
    matches = []
    for line in commands.splitlines():
        # Ninja also returns upstream archive commands containing legitimate
        # shell scaffolding. They are metadata only and are never executed by
        # this helper; apply the strict argv rule to the selected linker alone.
        raw = shlex.split(line)
        if any(raw[index] == "-o" and raw[index + 1] == TARGET
               for index in range(len(raw) - 1)):
            matches.append(command_tokens(line))
    if len(matches) != 1:
        raise RuntimeError("missing or ambiguous CTest link recipe")
    tokens = matches[0]
    actual_objects = [token for token in tokens if token.endswith(".o")]
    expected_tests = list(ORIGINAL_FIXTURE_OBJECTS)
    expected_objects = expected_tests + list(LINK_PRODUCTION_OBJECTS)
    if sorted(original_objects) != sorted(expected_tests) \
            or len(set(original_objects)) != len(expected_tests) \
            or sorted(actual_objects) != sorted(expected_objects):
        raise RuntimeError("CTest link must contain exactly its declared test and production owner objects")
    providers = provider_objects or []
    if len(set(providers)) != len(providers) or any(
            not name.endswith(".o") or name in expected_objects or name == str(fixture_object)
            for name in providers):
        raise RuntimeError("invalid or duplicate provider object")
    if any(Path(token).name == "libarch_solver_dispatch.a" for token in tokens):
        raise RuntimeError("Runtime-only fixture must not link the full Dispatch archive")
    result = []
    inserted = False
    for token in tokens:
        if token in original_objects:
            if not inserted:
                result.append(str(fixture_object))
                result.extend(providers)
                inserted = True
        else:
            result.append(token)
    return replace_option(result, "-o", str(executable))


def _metadata_command(tokens: list[str], build: Path) -> subprocess.CompletedProcess[str]:
    """Run only a bounded declared metadata command, retaining failure diagnostics."""
    result = subprocess.run(tokens, cwd=build, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            timeout=30, check=False)
    if result.returncode or len(result.stdout) > 262144:
        raise RuntimeError("cannot inspect CTest owner metadata: " + result.stdout[:4096])
    return result


def _query_dependencies(text: str, target: str, rule: str) -> tuple[list[str], list[str]]:
    """Parse actual Ninja query input/output lines; reject an unexpected owner rule."""
    lines = text.splitlines()
    if lines[:2] != [target + ":", "  input: " + rule] or lines.count("  outputs:") != 1:
        raise RuntimeError("unexpected Ninja metadata owner: " + target)
    separator = lines.index("  outputs:")
    inputs, outputs = [], []
    for index, line in enumerate(lines[2:], 2):
        if index == separator:
            continue
        if not line.startswith("    ") or not line.strip():
            raise RuntimeError("unsupported Ninja dependency query syntax")
        value = line[4:]
        if index < separator:
            if value.startswith("|| "):
                value = value[3:]
            elif value.startswith("| "):
                value = value[2:]
            inputs.append(value)
        else:
            outputs.append(value)
    return inputs, outputs


def _require_metadata_inputs(build: Path, inputs: list[str], manifest_mtime_ns: int,
                             force: Path) -> dict[str, list[int]]:
    """Check every real regeneration prerequisite; skip only the proven phony force."""
    observations = {}
    for name in inputs:
        path = Path(name)
        path = path if path.is_absolute() else build / path
        path = path.resolve()
        if path == force:
            continue
        if not path.is_file():
            raise RuntimeError("missing real CMake regeneration input: " + str(path))
        observation = _observation(path)
        if observation[3] > manifest_mtime_ns:
            raise RuntimeError("CMake regeneration input newer than build.ninja: " + str(path))
        observations[str(path)] = list(observation)
    return observations


def _verify_glob_script(script: Path, stamp: Path) -> None:
    """Permit only CMake's literal generated GLOB comparison and exact stamp touch.

    This script scan may touch its configured metadata stamp on a mismatch; it
    cannot configure, compile, include another script or execute a process.
    """
    original = script.read_text()
    if not original.startswith("# CMAKE generated file: DO NOT EDIT!\n"):
        raise RuntimeError("glob metadata script is not generated by CMake")
    text = "\n".join(line for line in original.splitlines()
                     if line.strip() and not line.lstrip().startswith("#"))
    literal = r'[^"\n$;\\]*'
    block = (r'file\(GLOB(?:_RECURSE)? NEW_GLOB LIST_DIRECTORIES (?:true|false) "'
             + literal + r'"\)\nset\(OLD_GLOB\n(?:  "' + literal + r'"\n)*  \)\n'
             + r'if\(NOT "\$\{NEW_GLOB\}" STREQUAL "\$\{OLD_GLOB\}"\)\n'
             + r'  message\("-- GLOB mismatch!"\)\n  file\(TOUCH_NOCREATE "'
             + re.escape(str(stamp)) + r'"\)\nendif\(\)')
    if not re.fullmatch(r'cmake_policy\(SET CMP0009 NEW\)\n(?:' + block + r'\n?)+', text):
        raise RuntimeError("unsupported or unsafe generated glob metadata syntax")


def _require_no_target_work(output: str) -> None:
    """The exact-manifest dry run must report no pending real target operation."""
    for line in output.splitlines():
        if line != "ninja: no work to do.":
            raise RuntimeError("CTest owner is not frozen/fresh; refresh the existing build first: "
                               + output[:4096])
    if not output.strip():
        raise RuntimeError("empty Ninja target freshness evidence")


def require_fresh_target(build: Path, compiled_objects: list[str] | None = None) -> dict[str, Any]:
    """Prove freshness without Ninja's forced glob dry-run regeneration artifact.

    Scan only the configured GLOB metadata, verify every real RERUN_CMAKE input,
    then dry-run byte-identical manifest metadata under another temporary name.
    The original manifest/rules and all production objects remain untouched;
    pending source/configure/compile/link work is rejected, never auto-built.
    """
    build = build.resolve()
    manifest = build / "build.ninja"
    script = build / "CMakeFiles/VerifyGlobs.cmake"
    stamp = build / "CMakeFiles/cmake.verify_globs"
    force = build / "CMakeFiles/VerifyGlobs.cmake_force"
    rules = build / "CMakeFiles/rules.ninja"
    manifest_identity = _stable_file_identity(manifest)
    rules_identity = _stable_file_identity(rules)
    query = _metadata_command(["ninja", "-t", "query", "build.ninja"], build).stdout
    inputs, _ = _query_dependencies(query, "build.ninja", "RERUN_CMAKE")
    force_query = _metadata_command(["ninja", "-t", "query", str(force)], build).stdout
    force_inputs, force_outputs = _query_dependencies(force_query, str(force), "phony")
    stamp_query = _metadata_command(["ninja", "-t", "query", str(stamp)], build).stdout
    stamp_inputs, stamp_outputs = _query_dependencies(stamp_query, str(stamp), "VERIFY_GLOBS")
    if force_inputs or force_outputs != [str(stamp)] or stamp_inputs != [str(force)] \
            or stamp_outputs != ["build.ninja"] or str(script) not in inputs or str(stamp) not in inputs:
        raise RuntimeError("unproved CMake glob force/metadata relationship")
    before = _require_metadata_inputs(build, inputs, manifest_identity["stat"][3], force)
    _verify_glob_script(script, stamp)
    scan_argv = command_tokens(_metadata_command(
        ["ninja", "-t", "commands", str(stamp)], build).stdout.strip())
    if len(scan_argv) != 3 or not Path(scan_argv[0]).is_absolute() \
            or Path(scan_argv[0]).name != "cmake" or scan_argv[1:] != ["-P", str(script)]:
        raise RuntimeError("unexpected configured glob metadata command")
    scan = _metadata_command(scan_argv, build)
    after = _require_metadata_inputs(build, inputs, manifest_identity["stat"][3], force)
    if scan.stdout or before != after:
        raise RuntimeError("configured glob scan found changed metadata; refresh the existing build first")
    if _stable_file_identity(manifest) != manifest_identity or _stable_file_identity(rules) != rules_identity:
        raise RuntimeError("production Ninja manifest/rules changed during metadata scan")
    manifest_bytes = manifest.read_bytes()
    with tempfile.TemporaryDirectory(prefix="arch-fixture-ninja-metadata-") as directory:
        copied = Path(directory) / "build.ninja"
        copied.write_bytes(manifest_bytes)
        copied_identity = _stable_file_identity(copied)
        if copied_identity["sha256"] != manifest_identity["sha256"]:
            raise RuntimeError("temporary metadata manifest differs from production")
        dry_argv = ["ninja", "-f", str(copied), "-n", TARGET, *(compiled_objects or [])]
        result = _metadata_command(dry_argv, build)
        _require_no_target_work(result.stdout)
        if _stable_file_identity(copied) != copied_identity:
            raise RuntimeError("temporary metadata manifest changed during dry run")
    if before != _require_metadata_inputs(build, inputs, manifest_identity["stat"][3], force) \
            or _stable_file_identity(manifest) != manifest_identity \
            or _stable_file_identity(rules) != rules_identity:
        raise RuntimeError("source/configure metadata changed during target dry run")
    return {"manifestIdentity": manifest_identity, "rulesIdentity": rules_identity,
            "rerunCmakeQuery": query, "verifiedPhonyForceQuery": force_query,
            "globStampQuery": stamp_query, "rerunInputObservations": before,
            "globScanArgv": scan_argv, "globScanOutput": scan.stdout,
            "dryRunArgv": dry_argv, "dryRunOutput": result.stdout,
            "temporaryManifestSha256": copied_identity["sha256"]}


def _observation(path: Path) -> tuple[int, int, int, int, int]:
    """Observe the actual allocation/file identity separately from content bytes."""
    info = path.stat()
    return info.st_dev, info.st_ino, info.st_size, info.st_mtime_ns, info.st_ctime_ns


def _stable_file_identity(path: Path) -> dict[str, Any]:
    """Reuse content hashing while retaining before/after inode and stat identity."""
    before = _observation(path)
    identity = provenance.file_identity(path)
    if before != _observation(path):
        raise RuntimeError(f"fixture input changed while capturing: {path}")
    return {**identity, "stat": list(before)}


def capture_inputs(root: Path, paths: list[Path]) -> dict[str, Any]:
    """Reuse code-only and streamed file identity; exclude raw simulation outputs."""
    files = {}
    for path in sorted({path.resolve() for path in paths}):
        files[str(path)] = _stable_file_identity(path)
    return {"source": provenance.source_identity(root), "files": files}


def require_unchanged(before: dict[str, Any], after: dict[str, Any]) -> None:
    """A private fixture cannot accept a changed source, object/archive or recipe."""
    if before != after:
        raise RuntimeError("source/compiled-owner fixture baseline changed")


def require_production_contract(entry: dict[str, Any], production: dict[str, Any]) -> None:
    """Require exact CPU/physics/optimization controls of the current production recipe."""
    actual, reference = command_tokens(entry["command"]), command_tokens(production["command"])
    def controls(tokens):
        return [t for t in tokens if t.startswith(("-f", "-O", "-march=", "-mtune=", "-std=", "-DARCH_"))
                and not t.startswith("-DARCH_IDENTITY_BUILD_CONFIG=")] + [t for t in tokens if t == "-DNDEBUG"]
    if "-DARCH_CUDA_BUILD_ENABLED=0" not in actual or not {
            "-fno-fast-math", "-fno-math-errno", "-ffp-contract=off"}.issubset(actual) \
            or controls(actual) != controls(reference) or actual[0] != reference[0]:
        raise RuntimeError("provider CPU/strict-FP/LTO/physics compile contract differs from production")


def selected_providers(entries: list[dict[str, Any]], root: Path, build: Path,
                       sources: dict[str, str], production: dict[str, Any]) -> list[dict[str, Any]]:
    """Select only approved existing source/target objects, without compiling an owner."""
    providers = []
    for name, owner in sources.items():
        if REUSABLE_SOURCES.get(name) != owner:
            raise RuntimeError("unapproved reused source/target owner: " + name)
        source = (root / name).resolve()
        entry = unique_entry(entries, source, owner)
        obj = (build / entry["output"]).resolve()
        if Path(entry["directory"]).resolve() != build or not source.is_file() or not source.is_relative_to(root) \
                or not obj.is_relative_to(build / "CMakeFiles" / (owner + ".dir")) or not obj.is_file():
            raise RuntimeError("provider source/object belongs to another build or is missing")
        argv = command_tokens(entry["command"])
        if argv.count("-c") != 1 or argv.count("-o") != 1 \
                or Path(argv[argv.index("-c") + 1]).resolve() != source \
                or (build / argv[argv.index("-o") + 1]).resolve() != obj:
            raise RuntimeError("provider compile operands disagree with compile database")
        require_production_contract(entry, production)
        providers.append({"source": name, "owner": owner, "object": str(obj),
                          "ninjaTarget": str(obj.relative_to(build)), "compileEntry": entry})
    return providers


def selected_link_owners(entries: list[dict[str, Any]], root: Path, build: Path,
                         template: dict[str, Any]) -> list[dict[str, Any]]:
    """Authenticate the two real production TUs retained from this exact target.

    Their configured target flags remain literal; they are not private fixtures
    or newly recompiled production. Borrowed objects and their real source bytes
    are frozen/hash-checked before and after compilation, linking and execution.
    """
    result = []
    for name, expected_object in zip(LINK_PRODUCTION_SOURCES, LINK_PRODUCTION_OBJECTS):
        source = (root / name).resolve()
        entry = unique_entry(entries, source, TARGET)
        obj = (build / entry["output"]).resolve()
        if (Path(entry["directory"]).resolve() != build or entry["output"] != expected_object
                or not source.is_file() or not source.is_relative_to(root)
                or not obj.is_relative_to(build / "CMakeFiles" / (TARGET + ".dir")) or not obj.is_file()):
            raise RuntimeError("retained production source/object belongs to another owner or is missing")
        argv = command_tokens(entry["command"])
        if (argv.count("-c") != 1 or argv.count("-o") != 1
                or Path(argv[argv.index("-c") + 1]).resolve() != source
                or (build / argv[argv.index("-o") + 1]).resolve() != obj):
            raise RuntimeError("retained production compile operands disagree with compile database")
        # These are the original CTest owner's real production TUs; compare
        # against that exact owner's configured strict CPU compile controls.
        require_production_contract(entry, template)
        result.append({"source": name, "owner": TARGET, "object": str(obj),
                       "ninjaTarget": expected_object, "compileEntry": entry})
    return result


def fixture_identity_defines(root: Path, source: Path, enabled: bool) -> list[str]:
    """Bind only the two real IO fixtures to their own exact source path and SHA."""
    if not isinstance(enabled, bool):
        raise RuntimeError("IO fixture identity selector must be boolean")
    if not enabled:
        return []
    if not source.is_relative_to(root) or str(source.relative_to(root)) not in IO_FIXTURES:
        raise RuntimeError("IO identity is restricted to the two approved real fixtures")
    return [f'-DARCH_IO_FIXTURE_SOURCE_FILE="{source}"',
            f'-DARCH_IO_FIXTURE_SOURCE_SHA256="{provenance.sha256(source)}"']


def _execute_logged(label: str, tokens: list[str], build: Path, output: Path) -> None:
    """Run one declared fixture compiler/linker and retain its complete local log."""
    log_path = output / (label + ".log")
    with log_path.open("w") as log:
        process = subprocess.Popen(tokens, cwd=build, stdout=log,
                                   stderr=subprocess.STDOUT, start_new_session=True)
        try:
            code = process.wait(timeout=180)
        except subprocess.TimeoutExpired:
            os.killpg(process.pid, signal.SIGTERM)
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                os.killpg(process.pid, signal.SIGKILL)
                process.wait()
            raise RuntimeError(f"fixture {label} timed out; retained log: {log_path}")
    if code:
        raise RuntimeError(f"fixture {label} failed ({code}); retained log: {log_path}")


def build_cpu_fixture(*, build: Path, output: Path, source: Path,
                      executable_name: str, owner_sources: list[str],
                      observed_headers: list[str], reuse_compiled_sources: dict[str, str] | None = None,
                      compile_recipe: str = "stage_contract", io_fixture_identity: bool = False
                      ) -> tuple[Path, dict[str, Any], dict[str, Any]]:
    """Compile only the fixture and reuse a frozen real CTest library owner.

    This is a corrected engineering link shape, not proof that an earlier LTO
    problem is fixed or that native-RZ physical release qualification passed.
    Caller retains the same CLI/output root and separately executes its fixture.
    """
    root = Path(__file__).resolve().parents[1]
    build, output, source = build.resolve(), output.resolve(), source.resolve()
    if output.exists():
        raise RuntimeError("output-root must be new")
    if not source.is_relative_to(root / "tests") or not source.is_file():
        raise RuntimeError("fixture must be a real source in the single project checkout")
    entries = json.loads((build / "compile_commands.json").read_text())
    production = unique_entry(entries, root / "src/main.cpp", "ARCH")
    originals = [unique_entry(entries, root / name, TARGET) for name in ORIGINAL_FIXTURES]
    if any(Path(entry["directory"]).resolve() != build for entry in [production, *originals]):
        raise RuntimeError("compile recipe belongs to another build directory")
    providers = selected_providers(entries, root, build, reuse_compiled_sources or {}, production)
    retained = selected_link_owners(entries, root, build, originals[0])
    freshness = require_fresh_target(build, [p["ninjaTarget"] for p in providers + retained])
    commands = subprocess.check_output(["ninja", "-t", "commands", TARGET],
                                       cwd=build, text=True, timeout=30)
    obj, exe = output / "0.o", output / executable_name
    compile_tokens, additions = fixture_compile_recipe(originals[0], production, source, obj,
                                                       recipe=compile_recipe)
    identity_defines = fixture_identity_defines(root, source, io_fixture_identity)
    compile_tokens += identity_defines
    link_tokens = fixture_link_recipe(commands, [entry["output"] for entry in originals], obj, exe,
                                      [p["object"] for p in providers])
    libraries = []
    for token in link_tokens:
        if re.search(r"\.(?:a|so(?:\.\d+)*)$", token):
            candidate = Path(token)
            libraries.append(candidate if candidate.is_absolute() else build / candidate)
    compiler = Path(compile_tokens[0])
    linker = Path(link_tokens[0])
    if not compiler.is_absolute() or not linker.is_absolute():
        raise RuntimeError("configured fixture compiler/linker must have explicit absolute paths")
    paths = [build / name for name in ("CMakeCache.txt", "compile_commands.json", "build.ninja")]
    paths += [build / "CMakeFiles/rules.ninja"]
    paths += [Path(name) for name in freshness["rerunInputObservations"]]
    paths += [compiler, linker]
    paths += [source, *libraries, *(root / name for name in owner_sources + observed_headers)]
    paths += [path for p in providers + retained for path in (root / p["source"], Path(p["object"]))]
    before = capture_inputs(root, paths)
    if identity_defines and identity_defines[1] != \
            f'-DARCH_IO_FIXTURE_SOURCE_SHA256="{before["files"][str(source)]["sha256"]}"':
        raise RuntimeError("IO fixture source changed while binding its identity")
    for name, observed in freshness["rerunInputObservations"].items():
        if before["files"][name]["stat"] != observed:
            raise RuntimeError("CMake input changed after target freshness check: " + name)
    output.mkdir(parents=True)
    record = {"target": TARGET, "compileRecipe": compile_recipe,
              "compileRecipeSource": production["file"] if compile_recipe == "production" else originals[0]["file"],
              "compileArgv": compile_tokens, "linkArgv": link_tokens,
              "productionOpenMPControlsAdded": additions,
              "standaloneFixtureDefinesRemoved": [EMBEDDED_FIXTURE_DEFINE]
                  if EMBEDDED_FIXTURE_DEFINE in command_tokens(
                      production["command"] if compile_recipe == "production" else originals[0]["command"])
                  else [],
              "replacedTestOwnerSources": list(ORIGINAL_FIXTURES),
              "retainedCompiledOwnerSources": [{**p,
                  "sourceSha256": before["files"][str((root / p["source"]).resolve())]["sha256"],
                  "objectSha256": before["files"][p["object"]]["sha256"]} for p in retained],
              "openMPReason": "preserve previous runner's explicitly declared production OpenMP" if additions else None,
              "freshnessDryRun": freshness,
              "reusedCompiledSources": providers,
              "reusedCompiledOwner": {"name": "arch_driver_runtime",
                                      "sourceSha256": {name: before["files"][str((root / name).resolve())]["sha256"]
                                                       for name in owner_sources}},
              "inputs": before, "scienceQualification": "not-established"}
    (output / "fixture-build-inputs.json").write_text(json.dumps(record, indent=2) + "\n")
    _execute_logged("compile-0", compile_tokens, build, output)
    require_unchanged(before, capture_inputs(root, paths))
    _execute_logged("link", link_tokens, build, output)
    require_unchanged(before, capture_inputs(root, paths))
    with exe.open("rb") as stream:
        if stream.read(4) != b"\x7fELF":
            raise RuntimeError("fixture link did not produce an ELF executable")
    record["executableIdentity"] = _stable_file_identity(exe)
    (output / "fixture-build-inputs.json").write_text(json.dumps(record, indent=2) + "\n")
    return exe, record, {"root": root, "paths": paths, "before": before}


def verify_fixture_inputs(frozen: dict[str, Any], executable: Path,
                          expected_executable: dict[str, Any]) -> None:
    """Reject source/library or ELF replacement during the actual private run."""
    require_unchanged(frozen["before"], capture_inputs(frozen["root"], frozen["paths"]))
    if _stable_file_identity(executable) != expected_executable:
        raise RuntimeError("fixture executable changed during execution")
