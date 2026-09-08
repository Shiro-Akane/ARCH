"""Shared, fail-closed identity for runtime validation evidence.

This records the *observed* source/build/artifacts, not a reproducible-build
attestation. Code and validation inputs (including untracked code under the
source directories) are hashed. Documentation, stored results, build/output
trees and unrelated untracked user files are deliberately outside that scope.
In particular, untracked simulation/*.cpp files are NOT ignored: CMake globs
them into ARCH. Output directories must not overwrite any source input.
"""

from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
from typing import Any
from collections.abc import Callable, Mapping


SOURCE_SUFFIXES = {
    ".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx",
    ".cu", ".cuh", ".inc", ".ipp", ".tpp", ".def", ".cmake", ".in",
    ".py", ".sh", ".json", ".par", ".yaml", ".yml",
}
SOURCE_DIRECTORIES = {"src", "simulation", "tests", "tools", "cmake", "validation"}
ROOT_CONFIGURATION = {"CMakeLists.txt", "CMakePresets.json", "CMakeUserPresets.json"}


def execution_environment_identity() -> dict[str, str | None]:
    # Only execution controls, not a dump of credentials or unrelated user
    # environment. Current formal runners inherit this environment unchanged.
    # None means unspecified, not a claim about the runtime's chosen default.
    return {name: os.environ.get(name) for name in (
        "OMP_NUM_THREADS", "OMP_DYNAMIC", "OMP_PROC_BIND", "OMP_PLACES",
        "OMP_SCHEDULE", "CUDA_VISIBLE_DEVICES", "CUDA_MODULE_LOADING")}


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def file_identity(path: Path) -> dict[str, str]:
    """Hash one declared dependency and reject replacement during observation."""
    def observation():
        stat = path.stat()
        return (stat.st_dev, stat.st_ino, stat.st_size, stat.st_mtime_ns, stat.st_ctime_ns)
    if not path.is_file():
        raise RuntimeError(f"missing runtime validation input: {path}")
    before = observation()
    result = {"path": str(path.resolve()), "sha256": sha256(path)}
    if before != observation():
        raise RuntimeError(f"runtime validation input changed while hashing: {path}")
    return result


def runtime_inputs(*, parameter_file: Path, working_directory: Path,
                   parameter_reader: Callable[[Path], dict[str, str]],
                   scientific_overrides: Mapping[str, Any] | None = None) -> dict[str, Any]:
    """Fingerprint the canonical input and the table ARCH will actually open.

    The caller injects the existing validation parameter parser, avoiding a
    second parser (or an import cycle with validate_backend_results). Scientific
    overrides have the same last-value-wins ordering as parameter rendering.
    Relative table paths are relative to ARCH's subprocess cwd, NOT the .par
    directory. Only an active table is read: ideal EOS does not consume tables.
    This is dependency identity, not a replacement for runtime policy selection.
    """
    def observation(path: Path) -> tuple[int, int, int, int, int]:
        stat = path.stat()
        return (stat.st_dev, stat.st_ino, stat.st_size,
                stat.st_mtime_ns, stat.st_ctime_ns)

    parameter_file = parameter_file.resolve()
    parameter_identity = file_identity(parameter_file)
    parameter_observation = observation(parameter_file)
    effective = dict(parameter_reader(parameter_file))
    overrides = {}
    for key, value in (scientific_overrides or {}).items():
        text = str(value)
        # The canonical .par goes through the shared parser, including comments.
        # Manifest overrides must be single, unambiguous values; refusing inline
        # comments/newlines prevents hidden assignments with different hashes.
        if not isinstance(key, str) or not key or key.strip() != key \
                or any(character in key for character in "=#\r\n") \
                or any(character in text for character in "#\r\n"):
            raise RuntimeError("scientific overrides must be single parameter values")
        overrides[key] = text.strip()
    effective.update(overrides)
    eos_type = effective.get("eos_type", "ideal").lower()
    dependencies = []
    if eos_type != "ideal":
        # EOSFactory::table_path and checkpoint provenance remove quote marks;
        # preserve all other path characters, including spaces and case.
        table = effective.get("eos_table_path", "").replace('"', "").replace("'", "")
        if not table:
            raise RuntimeError("non-ideal EOS requires eos_table_path for validation identity")
        path = Path(table)
        if not path.is_absolute():
            path = working_directory.resolve() / path
        dependencies.append({"parameter": "eos_table_path", **file_identity(path)})
    if parameter_observation != observation(parameter_file) \
            or parameter_identity["sha256"] != sha256(parameter_file):
        raise RuntimeError("canonical parameter input changed while recording runtime dependencies")
    return {
        "schema": 1,
        "parameter_file": parameter_identity["path"],
        "parameter_sha256": parameter_identity["sha256"],
        "scientific_overrides": dict(sorted(overrides.items())),
        "eos_type": eos_type,
        "dependencies": dependencies,
    }


def _git(root: Path, *arguments: str) -> bytes:
    result = subprocess.run(
        ["git", "-C", str(root), *arguments], check=False,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if result.returncode:
        raise RuntimeError("validation source identity requires a Git checkout: "
                           + result.stderr.decode(errors="replace").strip())
    return result.stdout


def _source_path(name: str) -> bool:
    path = Path(name)
    if name in ROOT_CONFIGURATION:
        return True
    if not path.parts or path.parts[0] not in SOURCE_DIRECTORIES:
        return False
    if (path.parts[0] == "validation" and "results" in path.parts) \
            or "__pycache__" in path.parts:
        return False
    return path.suffix in SOURCE_SUFFIXES or path.name == "CMakeLists.txt"


def source_identity(root: Path) -> dict[str, Any]:
    root = root.resolve()
    if Path(_git(root, "rev-parse", "--show-toplevel").decode().strip()).resolve() != root:
        raise RuntimeError("--source-root must be the Git repository root")
    tracked = {
        name for name in _git(root, "ls-files", "-z", "--cached").decode().split("\0")
        if name and _source_path(name)
    }
    untracked = {
        # CMake's source glob also sees ignored, untracked .cpp files. Filter
        # by source role, not .gitignore, so those cannot escape the identity.
        name for name in _git(root, "ls-files", "-z", "--others")
        .decode().split("\0") if name and _source_path(name)
    }
    digest = hashlib.sha256()
    for name in sorted(tracked | untracked):
        path = root / name
        # Deleted tracked inputs must change identity too. Hash symlink target
        # contents as they are what the compiler/validator actually consumes.
        value = sha256(path) if path.is_file() else "missing"
        digest.update(name.encode() + b"\0" + value.encode() + b"\0")
    modified = sorted({
        name for name in _git(root, "diff", "HEAD", "--name-only", "-z").decode().split("\0")
        if name and _source_path(name)
    })
    return {
        "commit": _git(root, "rev-parse", "HEAD").decode().strip(),
        "scope": "code-and-validation-inputs-v1",
        "scope_policy": {
            "directories": sorted(SOURCE_DIRECTORIES),
            "suffixes": sorted(SOURCE_SUFFIXES),
            "root_configuration": sorted(ROOT_CONFIGURATION),
            "nested_cmakelists": True,
            "excluded": ["validation/**/results/**", "**/__pycache__/**"],
            "include_untracked_and_gitignored_source": True,
        },
        "worktree_sha256": digest.hexdigest(),
        "dirty": bool(modified or untracked),
        "modified_files": modified,
        "untracked_source_files": sorted(untracked),
    }


def registered_network_identity(build_dir: Path, options: Mapping[str, str]) -> list[dict[str, Any]]:
    """Fingerprint actual CMake-registered packages, never scan an external root.

    The generated include inventory is the build's authority. Selecting packages
    from a directory listing would also certify unregistered candidate networks.
    Hash all package files, including upstream .H headers and runtime rate data.
    """
    root_name = options.get("ARCH_CUSTOM_NETWORK_ROOT")
    if not root_name:
        return []
    root = Path(root_name).resolve()
    inventory = build_dir / "generated/CustomNetworks.generated.h"
    if not inventory.is_file():
        raise RuntimeError("missing configured custom-network include inventory")
    records = []
    for name in re.findall(r'^#include\s+"([^"]+)"\s*$', inventory.read_text(), re.MULTILINE):
        if name == "CustomNetworkRegistry.generated.h":
            continue
        header = Path(name)
        if not header.is_absolute():
            header = root / header
        header = header.resolve()
        package = header.parent
        if not header.is_file() or package.parent != root:
            raise RuntimeError(f"registered network header outside a declared package: {header}")
        manifest = package / "manifest.json"
        metadata = json.loads(manifest.read_text())
        files = []
        for path in sorted(package.rglob("*")):
            if "__pycache__" in path.parts or ".git" in path.parts:
                continue
            if path.is_symlink() and path.is_dir():
                raise RuntimeError(f"network directory symlinks need explicit dependency inventory: {path}")
            if path.is_file():
                files.append({"name": path.relative_to(package).as_posix(), **file_identity(path)})
        records.append({"package": package.name, "root": str(package),
                        "manifest": metadata, "files": files})
    if len({record["package"] for record in records}) != len(records):
        raise RuntimeError("duplicate registered network package")
    return records


def sparse_link_identity(build_dir: Path, options: dict[str, str], selected: str) -> dict[str, Any]:
    """Hash sparse-provider artifacts named by the actual final link command.

    Includes fetched KLU's separately linked static dependencies, which are
    absent from KLU_LIBRARY. No build command is executed. This describes the
    CMake link closure, not all runtime-loaded OS/driver libraries.
    """
    enabled = lambda name: options.get(name, "").upper() in {"ON", "TRUE", "1", "YES"}
    required = set()
    if enabled("ARCH_ENABLE_KLU"):
        required.add("klu")
    cudss_library = options.get("CuDSS_LIBRARY", "")
    if enabled("ARCH_ENABLE_CUDA") and enabled("ARCH_ENABLE_CUDSS") \
            and cudss_library and not cudss_library.endswith("-NOTFOUND"):
        required.add("cudss")
    if not required:
        return {"required_providers": [], "libraries": []}
    output = Path(options.get("ARCH_RUNTIME_OUTPUT_DIRECTORY", ""))
    if not output.is_absolute():
        output = build_dir / output
    if options.get("CMAKE_CONFIGURATION_TYPES"):
        output /= selected
    output /= "ARCH"
    generator = options.get("CMAKE_GENERATOR", "")
    if generator.startswith("Ninja"):
        command = [options.get("CMAKE_MAKE_PROGRAM", "ninja"), "-C", str(build_dir)]
        if options.get("CMAKE_CONFIGURATION_TYPES"):
            command += ["-f", f"build-{selected}.ninja"]
        target = str(output.resolve().relative_to(build_dir.resolve()))
        result = subprocess.run(command + ["-t", "commands", "-s", target],
                                check=True, capture_output=True, text=True)
        link_command = result.stdout.strip()
    elif generator == "Unix Makefiles":
        link_command = (build_dir / "CMakeFiles/ARCH.dir/link.txt").read_text(encoding="utf-8").strip()
    else:
        raise RuntimeError("sparse link qualification currently requires Ninja or Unix Makefiles")
    return sparse_link_command_identity(build_dir, link_command, required)


def sparse_link_command_identity(build_dir: Path, command: str, required: set[str]) -> dict[str, Any]:
    libraries, observed = {}, set()
    pattern = re.compile(r'lib(klu|amd|btf|colamd|suitesparseconfig|cudss|cublas|cublasLt)\.(?:a|so(?:\.\d+)*)$')
    for argument in shlex.split(command):
        match = pattern.fullmatch(Path(argument).name)
        if match:
            path = Path(argument)
            if not path.is_absolute():
                path = build_dir / path
            identity = file_identity(path)
            libraries[identity["path"]] = identity
            observed.add(match.group(1))
    if not required.issubset(observed):
        raise RuntimeError("final link does not name all required sparse provider artifacts")
    return {"required_providers": sorted(required),
            "link_command_sha256": hashlib.sha256(command.encode()).hexdigest(),
            "libraries": [libraries[path] for path in sorted(libraries)]}


def build_identity(build_dir: Path, configuration: str | None = None) -> dict[str, Any]:
    cache = build_dir / "CMakeCache.txt"
    commands = build_dir / "compile_commands.json"
    if not cache.is_file() or not commands.is_file():
        raise RuntimeError("--build-dir requires CMakeCache.txt and compile_commands.json")
    entries = {}
    source_directory = None
    for line in cache.read_text(encoding="utf-8").splitlines():
        if line.startswith(("#", "//")) or "=" not in line or ":" not in line:
            continue
        key_type, value = line.split("=", 1)
        key, kind = key_type.split(":", 1)
        if key == "CMAKE_HOME_DIRECTORY":
            source_directory = value
        if (kind != "INTERNAL" or key == "CMAKE_GENERATOR") and (key.startswith(("CMAKE_", "ARCH_", "CuDSS_", "KLU_", "CUDSS_"))
                                   or key == "BUILD_TESTING"):
            entries[key] = value
    configurations = entries.get("CMAKE_CONFIGURATION_TYPES", "")
    selected = configuration or entries.get("CMAKE_BUILD_TYPE", "")
    if not selected or (configurations and (
        configuration is None or selected not in configurations.split(";")
    )):
        raise RuntimeError("build configuration is missing or invalid; pass --configuration")
    if not configurations and selected != entries.get("CMAKE_BUILD_TYPE"):
        raise RuntimeError("--configuration differs from the single-config CMake build")
    return {
        "source_directory": source_directory,
        "configuration": selected,
        "multi_config": bool(configurations),
        "cmake_cache_sha256": sha256(cache),
        "compile_commands_sha256": sha256(commands),
        "cmake_options": entries,
        "registered_networks": registered_network_identity(build_dir, entries),
        "sparse_link": sparse_link_identity(build_dir, entries, selected),
        "configured_sparse_libraries": {
            key: file_identity(Path(value))
            for key, value in entries.items()
            if key in {"CuDSS_LIBRARY", "KLU_LIBRARY"}
            and value and not value.endswith("-NOTFOUND")
        },
    }


def _configured_artifact_paths(build_dir: Path, build: dict[str, Any]) -> dict[str, Path]:
    """Bind qualification to this build tree, not an arbitrary copied binary."""
    build_dir = build_dir.resolve()
    runtime_directory = build["cmake_options"].get("ARCH_RUNTIME_OUTPUT_DIRECTORY")
    if not runtime_directory or "$<" in runtime_directory:
        raise RuntimeError("qualification requires an explicit build-local "
                           "ARCH_RUNTIME_OUTPUT_DIRECTORY without generator expressions")
    output = Path(runtime_directory)
    if not output.is_absolute():
        output = build_dir / output
    if not output.resolve().is_relative_to(build_dir):
        raise RuntimeError("ARCH_RUNTIME_OUTPUT_DIRECTORY must be beneath --build-dir; "
                           "configure a build-local output directory for qualification")
    comparator_output = build_dir
    if build["multi_config"]:
        output = output / build["configuration"]
        comparator_output = comparator_output / build["configuration"]
    result = {
        "arch": (output / "ARCH").resolve(),
        "checkpoint_validator": (comparator_output / "arch_cuda_single_level_validation").resolve(),
    }
    if any(not path.is_relative_to(build_dir) for path in result.values()):
        raise RuntimeError("configured validation artifacts must remain inside --build-dir")
    return result


def _artifact_observations(artifacts: Mapping[str, Path]) -> dict[str, list[int]]:
    result = {}
    for name, path in artifacts.items():
        stat = path.stat()
        result[name] = [stat.st_dev, stat.st_ino, stat.st_size,
                        stat.st_mtime_ns, stat.st_ctime_ns]
    return result


def capture_focused(*, artifacts: Mapping[str, Path], source_root: Path,
                    build_dir: Path, configuration: str | None = None) -> dict[str, Any]:
    """Observe named build-local test artifacts, not an ARCH release certificate.

    Reuse the full source/package/library identity and replacement guards. The
    caller must still verify its scientific/route coverage and record its scope.
    """
    build = build_identity(build_dir, configuration)
    if build["source_directory"] is None or Path(build["source_directory"]).resolve() != source_root.resolve():
        raise RuntimeError("focused build belongs to a different --source-root")
    if not artifacts or any(not path.resolve().is_relative_to(build_dir.resolve())
                            for path in artifacts.values()):
        raise RuntimeError("focused artifacts must be nonempty and build-local")
    before = _artifact_observations(artifacts)
    result = {"schema": 1, "source": source_identity(source_root), "build": build,
              "execution_environment": execution_environment_identity(),
              "artifacts": {name: file_identity(path) for name, path in artifacts.items()},
              "artifact_observation": before}
    if before != _artifact_observations(artifacts):
        raise RuntimeError("focused artifacts changed while recording identity")
    return result


def capture(*, arch: Path, checkpoint_validator: Path, source_root: Path,
            build_dir: Path, configuration: str | None = None) -> dict[str, Any]:
    build = build_identity(build_dir, configuration)
    if build["source_directory"] is None or Path(
        build["source_directory"]
    ).resolve() != source_root.resolve():
        raise RuntimeError("CMake build belongs to a different --source-root")
    configured = _configured_artifact_paths(build_dir, build)
    for name, actual in (("arch", arch), ("checkpoint_validator", checkpoint_validator)):
        if actual.resolve() != configured[name]:
            raise RuntimeError(f"validation {name} must be the configured artifact "
                               f"in --build-dir: {configured[name]}")
    build["artifact_paths"] = {name: str(path) for name, path in configured.items()}

    artifacts = {"arch": arch, "checkpoint_validator": checkpoint_validator}
    before = _artifact_observations(artifacts)
    result = {
        "schema": 1,
        "source": source_identity(source_root),
        "build": build,
        "execution_environment": execution_environment_identity(),
        "artifacts": {
            "arch_sha256": sha256(arch),
            "checkpoint_validator_sha256": sha256(checkpoint_validator),
        },
    }
    after = _artifact_observations(artifacts)
    if before != after:
        raise RuntimeError("validation artifacts changed while recording identity")
    # Run-local inode/ctime guards catch replacement and edit-then-restore,
    # including when the final content hash happens to match the original.
    # They are not used for comparisons between separately collected reports.
    result["artifact_observation"] = before
    return result


def require_unchanged(before: dict[str, Any], after: dict[str, Any]) -> None:
    for section in ("artifacts", "artifact_observation", "source", "build", "execution_environment"):
        if before.get(section) != after.get(section):
            raise RuntimeError(f"validation {section} changed during execution; rerun validation")


def require_evidence_identity(evidence: dict[str, Any], expected: dict[str, Any]) -> None:
    if evidence.get("binary_sha256") != expected["artifacts"]["arch_sha256"]:
        raise RuntimeError("evidence ARCH SHA-256 differs from the final artifact")
    provenance = evidence.get("provenance")
    if evidence.get("schema") != 1 or not isinstance(provenance, dict) \
            or provenance.get("schema") != 1 \
            or evidence.get("identity_verified_after_run") is not True:
        raise RuntimeError("historical/incomplete provenance cannot qualify final artifacts")
    for section in ("artifacts", "source", "build", "execution_environment"):
        if provenance.get(section) != expected.get(section):
            raise RuntimeError(f"evidence {section} differs from the final qualification identity")


def require_final_artifact_list(path: Path, arch: Path, build_dir: Path | None = None) -> None:
    entries = []
    for line in path.read_text(encoding="utf-8").splitlines():
        parts = line.split(maxsplit=1)
        if len(parts) != 2:
            raise RuntimeError("malformed final artifact hash list")
        named = Path(parts[1].lstrip("*"))
        if named.name == arch.name:
            entries.append(parts[0])
        elif build_dir is not None:
            candidate = named if named.is_absolute() else build_dir / named
            if not candidate.is_file() or sha256(candidate) != parts[0]:
                raise RuntimeError(f"final artifact SHA-256 mismatch: {named}")
    if entries != [sha256(arch)]:
        raise RuntimeError("final artifact list must contain exactly one matching ARCH hash")


def write_evidence(path: Path, evidence: dict[str, Any], before: dict[str, Any],
                   **capture_arguments: Any) -> None:
    """Publish a success report only after every observed identity is rechecked."""
    require_unchanged(before, capture(**capture_arguments))
    evidence.update({
        "provenance": before,
        "binary_sha256": before["artifacts"]["arch_sha256"],
        "identity_verified_after_run": True,
    })
    path.write_text(json.dumps(evidence, indent=2, default=str) + "\n", encoding="utf-8")
