#!/usr/bin/env python3
"""Read-only source/build-graph comparison for the 2026-09-08 relocation.

Only the selected JSON report is written. This does not configure, compile, run
ARCH, or reinterpret the source-73 scientific/sanitizer evidence as a new run.
"""
from __future__ import annotations

import argparse
from collections import Counter
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path, PurePosixPath
import posixpath
import re
import sys
import tarfile


HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]
sys.path.insert(0, str(ROOT / "tools"))
import audit_architecture as audit
import validation_provenance as provenance


CPP_SUFFIXES = {".c", ".cc", ".cpp", ".cxx", ".cu", ".cuh", ".h", ".hh",
                ".hpp", ".hxx", ".inc", ".ipp", ".tpp", ".def", ".in"}
PRESENTATION_ONLY = {
    "src/numerics/burnsolver/ode_bd.h": "leading indentation and blank lines",
    "src/numerics/burnsolver/ode_ros4.h": "leading indentation and blank lines",
    "src/physics/eos/Tabular3DEOS.h": "description of the existing shared device implementation",
    "src/physics/eos/Tabular4DEOS.h": "description of the existing temperature table axis/shared implementation",
}
MECHANICAL_FILES = {"CMakeLists.txt", "cmake/CudaBurnSparseRoutes.cmake",
                    "tools/audit_combination_v2.py",
                    "tests/tooling/test_audit_combination_v2.py"}
LITERALS = re.compile(
    r'[rR]"([^ ()\\\t\r\n]{0,16})\(.*?\)\1"'
    r'|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'', re.DOTALL)
INCLUDE = re.compile(r'^[ \t]*#[ \t]*include[ \t]+"([^"\n]+)"[ \t]*$', re.MULTILINE)


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def stable_json(value) -> str:
    return json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=False)


def quoted_includes(content: str):
    """Find actual quoted directives, not lookalikes inside raw string literals."""
    code = audit._without_cpp_comments(content)
    literals = [(match.start(), match.end()) for match in LITERALS.finditer(code)]
    result = []
    for match in INCLUDE.finditer(code):
        marker = code.index("#", match.start(), match.end())
        if not any(start <= marker < end for start, end in literals):
            result.append((match.start(), match.end(), match.group(1)))
    return code, result


def without_quoted_includes(content: str, *, comments: bool) -> str:
    code, directives = quoted_includes(content)
    selected = code if comments else content
    for start, end, _ in reversed(directives):
        selected = selected[:start] + selected[end:]
    return selected


def normalized_cpp_body(content: str) -> str:
    code = without_quoted_includes(content, comments=True)
    # Preserve every byte in ordinary, character and multiline raw literals.
    # Only line-leading indentation and entirely blank/comment-only lines are
    # disregarded outside literals; operators/interior whitespace are retained.
    code = LITERALS.sub(lambda match: "\0LITERAL_SHA256_" +
                        digest(match.group().encode()) + "\0", code)
    return "\n".join(line.lstrip(" \t") for line in code.splitlines()
                     if line.strip(" \t"))


def resolve_internal(source: str, operand: str, names: set[str]):
    # Existing project search order: quoted include's own directory, src, then
    # repository root. Generated/provider headers are outside this source proof.
    candidates = [posixpath.normpath(posixpath.join(posixpath.dirname(source), operand)),
                  posixpath.normpath("src/" + operand), posixpath.normpath(operand)]
    for candidate in candidates:
        if candidate in names:
            return candidate
    return None


def compare_includes(old_path, new_path, old_text, new_text, old_names, new_names, mapping):
    old = quoted_includes(old_text)[1]
    new = quoted_includes(new_text)[1]
    errors, edges = [], []
    if len(old) != len(new):
        errors.append("quoted include count changed")
    for index, (before, after) in enumerate(zip(old, new)):
        target_before = resolve_internal(old_path, before[2], old_names)
        target_after = resolve_internal(new_path, after[2], new_names)
        expected = mapping.get(target_before, target_before)
        okay = (target_before is not None and target_after == expected) or (
            target_before is None and target_after is None and before[2] == after[2])
        edges.append({"order": index, "before_operand": before[2], "current_operand": after[2],
                      "before_internal_target": target_before,
                      "current_internal_target": target_after,
                      "expected_internal_target": expected, "equivalent": okay})
        if not okay:
            errors.append(f"quoted include {index} changed target or unresolved spelling")
    return edges, errors


def mechanical_paths(content: str, mapping: dict[str, str]) -> str:
    substitutions = dict(mapping)
    substitutions.update({old.removeprefix("src/"): new.removeprefix("src/")
                          for old, new in mapping.items()})
    substitutions.update({old.lower(): new.lower() for old, new in mapping.items()})
    pattern = re.compile("|".join(re.escape(old) for old in
                                  sorted(substitutions, key=len, reverse=True)))
    return pattern.sub(lambda match: substitutions[match.group()], content)


def fixture_paths(content: str, mapping: dict[str, str]):
    """Two exact dependency-preserving fixture adjustments, not rule changes."""
    helper_owner = mapping["src/cuda/runtime/CudaBackendBurn.h"]
    helper_destination = str(PurePosixPath(helper_owner).parent / "Helper.h")
    internal = mapping["src/cuda/runtime/CudaBackendInternal.h"].removeprefix("src/")
    sparse = mapping["src/cuda/runtime/CudaBackendBurnSparseImpl.cuh"]
    adjustments = [
        ('"src/cuda/runtime/Helper.h": \'#include "physics/eos/HelmEos.h"\\n\'',
         f'"{helper_destination}": \'#include "physics/eos/HelmEos.h"\\n\''),
        (f'"{sparse}": \'#include "CudaBackendInternal.h"\\n\'',
         f'"{sparse}": \'#include "{internal}"\\n\''),
    ]
    records = []
    for before, after in adjustments:
        if content.count(before) != 1:
            raise ValueError("expected exactly one declared fixture path adjustment")
        content = content.replace(before, after)
        records.append({"before": before, "current": after})
    old_owner = "src/cuda/runtime/CudaBackendBurn.h"
    old_helper = "src/cuda/runtime/Helper.h"
    old_sparse = "src/cuda/runtime/CudaBackendBurnSparseImpl.cuh"
    old_internal = "src/cuda/runtime/CudaBackendInternal.h"
    eos = "src/physics/eos/HelmEos.h"
    fixture_map = {**mapping, old_helper: helper_destination}
    before_names = {old_owner, old_helper, old_sparse, old_internal, eos}
    current_names = {fixture_map.get(name, name) for name in before_names}
    for record, owner, before_text, after_text in (
        (records[0], old_owner, '#include "Helper.h"', '#include "Helper.h"'),
        (records[0], old_helper, '#include "physics/eos/HelmEos.h"', '#include "physics/eos/HelmEos.h"'),
        (records[1], old_sparse, '#include "CudaBackendInternal.h"', f'#include "{internal}"'),
    ):
        edges, errors = compare_includes(owner, fixture_map.get(owner, owner),
                                         before_text, after_text, before_names,
                                         current_names, fixture_map)
        if errors:
            raise ValueError("fixture path adjustment changes the tested dependency edge")
        record.setdefault("resolved_fixture_edges", []).append(
            {"before_owner": owner, "current_owner": fixture_map.get(owner, owner), "edges": edges})
    return content, records


def source_names(root: Path) -> set[str]:
    return {name for option in ("--cached", "--others")
            for name in provenance._git(root, "ls-files", "-z", option).decode().split("\0")
            if name and provenance._source_path(name)}


def build_review(old: Path, new: Path, mapping):
    reverse = {value: key for key, value in mapping.items()}
    pattern = re.compile("|".join(re.escape(name) for name in
                                  sorted(reverse, key=len, reverse=True)))

    def normalize(text):
        text = text.replace(str(new), str(old))
        return pattern.sub(lambda match: reverse[match.group()], text)

    files = []
    for name in ("CMakeCache.txt", "compile_commands.json", "CTestTestfile.cmake", "CMakeFiles/rules.ninja", "build.ninja"):
        files.append({"relative_path": name, "before": provenance.file_identity(old / name),
                      "current": provenance.file_identity(new / name)})
    old_commands = json.loads((old / "compile_commands.json").read_text())
    new_commands = json.loads((new / "compile_commands.json").read_text())
    previous = Counter(stable_json(record) for record in old_commands)
    current = Counter(normalize(stable_json(record)) for record in new_commands)
    old_ctest = (old / "CTestTestfile.cmake").read_text()
    new_ctest = (new / "CTestTestfile.cmake").read_text()
    ctest_names = re.findall(r'^add_test\(\[=\[(.*?)\]=\]', old_ctest, re.MULTILINE)
    pools = []
    for build in (old, new):
        pools.append(re.findall(r'^pool ([^\n]+)\n  depth = ([0-9]+)$',
                                (build / "CMakeFiles/rules.ninja").read_text(), re.MULTILINE))
    old_rules = (old / "CMakeFiles/rules.ninja").read_text()
    new_rules = normalize((new / "CMakeFiles/rules.ninja").read_text())
    old_ninja = (old / "build.ninja").read_text()
    new_ninja = normalize((new / "build.ninja").read_text())
    link_rules = lambda text: [part for part in re.split(r'(?=^rule )', text, flags=re.MULTILINE)
                               if re.match(r'rule [^\n]*LINKER', part)]
    link_settings = lambda text: [line for line in text.splitlines()
                                  if line.startswith(("  LINK_FLAGS =", "  LINK_LIBRARIES ="))]
    before_link_rules, current_link_rules = link_rules(old_rules), link_rules(new_rules)
    before_link_settings, current_link_settings = link_settings(old_ninja), link_settings(new_ninja)
    errors = []
    if previous != current:
        errors.append("compile command multiset changed beyond declared build/source paths")
    if old_ctest != normalize(new_ctest):
        errors.append("CTest names/commands/properties changed beyond declared paths")
    if pools[0] != pools[1] or pools[0] != [("arch_cuda_heavy", "2")]:
        errors.append("Ninja heavy compile pool changed")
    if len(old_commands) != 280 or len(new_commands) != 280 or len(ctest_names) != 98:
        errors.append("unexpected frozen command/test inventory count")
    if before_link_rules != current_link_rules or before_link_settings != current_link_settings:
        errors.append("configured linker rules/flags/libraries changed beyond declared paths")
    for record in files:
        for side, build in (("before", old), ("current", new)):
            if provenance.file_identity(build / record["relative_path"]) != record[side]:
                errors.append("build configuration changed during review: " + record["relative_path"])
    commands = [{"before_source": record["file"],
                 "normalized_complete_record_sha256": digest(stable_json(record).encode())}
                for record in old_commands]
    return {"scope": "configured commands only; no build or test execution",
            "normalization": "new absolute build directory to old; inverse declared 44-path map only",
            "files": files, "compile_commands_before": len(old_commands),
            "compile_commands_current": len(new_commands), "compile_commands_exact_after_normalization": previous == current,
            "commands": commands, "unexpected_before_commands": list((previous - current).elements()),
            "unexpected_current_commands": list((current - previous).elements()),
            "ctest_names": ctest_names, "ctest_count": len(ctest_names),
            "ctest_file_exact_after_normalization": old_ctest == normalize(new_ctest),
            "ctest_normalized_sha256": digest(old_ctest.encode()),
            "ninja_pools_before": pools[0], "ninja_pools_current": pools[1],
            "linker_rules_count": len(before_link_rules),
            "linker_rules_exact_after_normalization": before_link_rules == current_link_rules,
            "linker_rules_normalized_sha256": digest(stable_json(before_link_rules).encode()),
            "link_flags_and_libraries_count": len(before_link_settings),
            "link_flags_and_libraries_exact_after_normalization": before_link_settings == current_link_settings,
            "link_flags_and_libraries_normalized_sha256": digest(stable_json(before_link_settings).encode()),
            "whole_build_ninja_exact_after_path_normalization": old_ninja == new_ninja,
            "whole_rules_ninja_exact_after_path_normalization": old_rules == new_rules,
            "whole_ninja_comparison_limit": "Not a claim of complete Ninja equivalence. The old rules contain an /usr/bin/time ARCH_COMPILE_METRIC wrapper absent in the fresh tree; fresh build metadata also has different PDB fields. These differences are retained, not silently normalized away. Compiler records, linker rules/settings, CTest and the heavy pool are checked separately.",
            "errors": errors}


def selftests():
    controls = []

    def check(name, condition):
        if not condition:
            raise AssertionError(name)
        controls.append({"name": name, "passed": True})

    norm = normalized_cpp_body
    check("leading indentation accepted", norm("  x += 1;\n") == norm("x += 1;\n"))
    check("blank lines accepted", norm("x;\n\ny;\n") == norm("x;\ny;\n"))
    check("standalone comments accepted", norm("// before\nx;\n") == norm("// after\nx;\n"))
    check("arithmetic mutation rejected", norm("x += 1;\n") != norm("x += 2;\n"))
    check("operator mutation rejected", norm("x += 1;\n") != norm("x -= 1;\n"))
    check("literal whitespace retained", norm('x = "a b";') != norm('x = "ab";'))
    check("raw literal indentation retained", norm('x = R"(a\n b)";') != norm('x = R"(a\nb)";'))
    check("raw literal include lookalike retained", not quoted_includes('x = R"(\n#include "fake.h"\n)";')[1])
    check("interior token whitespace retained", norm("a + b;") != norm("a+b;"))
    check("angle includes retained", norm("#include <a>\n") != norm("#include <b>\n"))
    check("quoted comments are literals", norm('x="//a";') != norm('x="//b";'))
    mapping = {"src/A.h": "src/group/A.h", "src/B.h": "src/group/B.h"}
    old_names, new_names = set(mapping), set(mapping.values())
    old = '#include "A.h"\n#include "B.h"\n'
    new = '#include "group/A.h"\n#include "group/B.h"\n'
    compare = lambda value: compare_includes("src/main.cpp", "src/main.cpp", old, value,
                                             old_names, new_names, mapping)[1]
    check("resolved include relocation accepted", not compare(new))
    check("include order change rejected", bool(compare('#include "group/B.h"\n#include "group/A.h"\n')))
    check("missing include target rejected", bool(compare(old)))
    check("include deletion rejected", bool(compare('#include "group/A.h"\n')))
    check("unknown external rename rejected", bool(compare_includes("x", "x", '#include "@a@"',
          '#include "@b@"', set(), set(), {})[1]))
    check("external spelling unchanged accepted", not compare_includes("x", "x", '#include "@a@"',
          '#include "@a@"', set(), set(), {})[1])
    check("mechanical normalization retains flags", mechanical_paths("src/A.h -O3", mapping) != "src/group/A.h -O0")
    check("mechanical normalization retains audit policy", mechanical_paths("src/A.h if rejected", mapping) != "src/group/A.h if accepted")
    return controls


def review(args):
    started = datetime.now(timezone.utc).isoformat()
    baseline_path, map_path = args.baseline.resolve(), args.path_map.resolve()
    baseline_identity = provenance.file_identity(baseline_path)
    map_identity = provenance.file_identity(map_path)
    baseline = json.loads(baseline_path.read_text())
    mapping = json.loads(map_path.read_text())["mapping"]
    if len(mapping) != 44 or len(set(mapping.values())) != 44:
        raise ValueError("expected 44 unique source moves")
    if any(not old.startswith("src/cuda/runtime/") or not new.startswith("src/cuda/runtime/")
           or PurePosixPath(old).name != PurePosixPath(new).name
           or ".." in PurePosixPath(new).parts or old == new for old, new in mapping.items()):
        raise ValueError("path map is not a basename-preserving runtime relocation")
    archive_path = Path(baseline["archive"]["path"])
    archive_identity = provenance.file_identity(archive_path)
    if archive_identity["sha256"] != baseline["archive"]["sha256"]:
        raise ValueError("baseline archive identity mismatch")
    records = {item["relative_path"]: item for item in baseline["files"]}
    if len(records) != len(baseline["files"]) or len(records) != 455:
        raise ValueError("unexpected baseline source inventory")
    old_names = {name for name, record in records.items() if "sha256" in record}
    old_missing = set(records) - old_names
    if len(old_names) != 452 or len(old_missing) != 3 or not set(mapping) <= old_names:
        raise ValueError("unexpected baseline present/missing/moved scope")
    expected_names = {mapping.get(name, name) for name in old_names}
    original = {}
    with tarfile.open(archive_path, "r:gz") as archive:
        members = archive.getmembers()
        if len(members) != len(old_names) or {member.name for member in members} != old_names:
            raise ValueError("archive inventory does not exactly match baseline existing files")
        for member in members:
            if not member.isfile() or ".." in PurePosixPath(member.name).parts or member.name.startswith("/"):
                raise ValueError("unsafe or non-regular archive member")
            content = archive.extractfile(member).read()
            if digest(content) != records[member.name]["sha256"]:
                raise ValueError("baseline member identity mismatch: " + member.name)
            original[member.name] = content
    before_digest = hashlib.sha256()
    for name, item in sorted(records.items()):
        before_digest.update(name.encode() + b"\0" + item.get("sha256", "missing").encode() + b"\0")
    if before_digest.hexdigest() != baseline["source"]["worktree_sha256"]:
        raise ValueError("baseline inventory does not reproduce original source fingerprint")
    source_before = provenance.source_identity(args.root)
    names = source_names(args.root)
    new_names = {name for name in names if (args.root / name).is_file()}
    missing = names - new_names
    problems = []
    extra, absent = sorted(new_names - expected_names), sorted(expected_names - new_names)
    if extra or absent:
        problems.append("current existing source scope differs from mapped baseline")
    old_residuals = sorted(name for name in mapping if (args.root / name).exists())
    if old_residuals:
        problems.append("moved files remain at old paths")
    if any((args.root / name).exists() for name in old_missing):
        problems.append("originally missing retired headers reappeared")
    unexpected_missing = sorted(missing - old_missing - set(mapping))
    if unexpected_missing:
        problems.append("unexplained missing tracked source inputs")
    file_results, fixture_adjustments = [], []
    for name in sorted(old_names):
        current_name = mapping.get(name, name)
        entry = {"before_path": name, "current_path": current_name,
                 "before_sha256": records[name]["sha256"], "moved": name != current_name}
        path = args.root / current_name
        if not path.is_file():
            entry.update({"status": "missing", "equivalent": False})
            file_results.append(entry)
            continue
        identity = provenance.file_identity(path)
        current_bytes = path.read_bytes()
        if digest(current_bytes) != identity["sha256"]:
            raise ValueError("source changed during comparison: " + current_name)
        old_bytes = original[name]
        entry.update({"current_sha256": identity["sha256"], "before_size": len(old_bytes),
                      "current_size": len(current_bytes), "byte_identical": old_bytes == current_bytes})
        errors = []
        if Path(name).suffix in CPP_SUFFIXES:
            before_text, current_text = old_bytes.decode(), current_bytes.decode()
            old_body, new_body = normalized_cpp_body(before_text), normalized_cpp_body(current_text)
            entry["body_normalized_before_sha256"] = digest(old_body.encode())
            entry["body_normalized_current_sha256"] = digest(new_body.encode())
            if old_body != new_body:
                errors.append("non-include implementation or literal changed")
            raw_body_changed = without_quoted_includes(before_text, comments=False) != without_quoted_includes(current_text, comments=False)
            if raw_body_changed and name not in PRESENTATION_ONLY:
                errors.append("undeclared non-include presentation change")
            entry["presentation_change"] = PRESENTATION_ONLY.get(name) if raw_body_changed else None
            if name in PRESENTATION_ONLY and "burnsolver" in name and raw_body_changed:
                indent = lambda text: "\n".join(line.lstrip(" \t") for line in text.splitlines() if line.strip(" \t"))
                if indent(before_text) != indent(current_text):
                    errors.append("ODE presentation change exceeds leading indentation/blank lines")
            edges, edge_errors = compare_includes(name, current_name, before_text, current_text,
                                                  old_names, new_names, mapping)
            entry["quoted_includes"] = edges
            errors.extend(edge_errors)
            entry["comparison"] = "literal-preserving body plus ordered resolved quoted includes"
        elif name in MECHANICAL_FILES:
            transformed = mechanical_paths(old_bytes.decode(), mapping)
            if name == "tests/tooling/test_audit_combination_v2.py":
                transformed, fixture_adjustments = fixture_paths(transformed, mapping)
            if transformed != current_bytes.decode():
                errors.append("CMake/auditor/fixture differs beyond exact mechanical path replacements")
            entry["mechanically_expected_sha256"] = digest(transformed.encode())
            entry["comparison"] = "exact bytes after declared path replacements"
        else:
            if old_bytes != current_bytes:
                errors.append("unrelated source or validation input changed")
            entry["comparison"] = "exact bytes"
        entry.update({"equivalent": not errors, "errors": errors})
        if errors:
            problems.append(current_name + ": " + "; ".join(errors))
        file_results.append(entry)
    builds = build_review(args.old_build.resolve(), args.new_build.resolve(), mapping)
    problems.extend(builds["errors"])
    source_after = provenance.source_identity(args.root)
    if source_before != source_after:
        problems.append("current source changed during review")
    if provenance.file_identity(archive_path) != archive_identity:
        problems.append("baseline archive changed during review")
    if provenance.file_identity(baseline_path) != baseline_identity or provenance.file_identity(map_path) != map_identity:
        problems.append("baseline or path map changed during review")
    helpers = [provenance.file_identity(ROOT / "tools" / name)
               for name in ("validation_provenance.py", "audit_architecture.py")]
    return {"schema": 1, "scope": "maintenance relocation source/configured-build equivalence",
            "started_utc": started, "completed_utc": datetime.now(timezone.utc).isoformat(),
            "status": "PASS" if not problems else "FAIL", "problems": problems,
            "limits": ["No configure/build/GPU/test campaign was run by this recipe.",
                       "Configured command equality does not establish rebuilt artifact identity or runtime correctness.",
                       "The source-73 scientific/sanitizer results retain their original source identity; they are not relabeled.",
                       "Unresolved external/generated quoted include spellings are preserved, not treated as source-owned headers.",
                       "Documentation, stored results, generated network packages and binaries are outside the source scope."],
            "recipe": provenance.file_identity(Path(__file__)),
            "inputs": {"baseline": baseline_identity, "archive": archive_identity,
                       "path_map": map_identity, "helpers": helpers},
            "baseline_source": baseline["source"], "current_source_before": source_before,
            "current_source_after": source_after, "mapping": mapping,
            "inventory": {"baseline_present": len(old_names), "current_present": len(new_names),
                          "mapped_moves": len(mapping), "original_missing": sorted(old_missing),
                          "current_missing_tracked": sorted(missing),
                          "moved_old_paths_absent": not old_residuals, "old_path_residuals": old_residuals,
                          "extra_current_inputs": extra, "absent_mapped_inputs": absent,
                          "unexpected_missing": unexpected_missing},
            "files": file_results, "fixture_dependency_adjustments": fixture_adjustments,
            "configured_build": builds, "negative_and_positive_controls": selftests()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=ROOT)
    parser.add_argument("--baseline", type=Path, default=HERE / "baseline.json")
    parser.add_argument("--path-map", type=Path, default=HERE / "runtime-path-map.json")
    parser.add_argument("--old-build", type=Path, default=ROOT / "build/release-core-throughput-cmake")
    parser.add_argument("--new-build", type=Path, default=ROOT / "build/maintenance-freeze-20260908/core")
    parser.add_argument("--output", type=Path, default=HERE / "relocation-review.json")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        controls = selftests()
        print(json.dumps({"status": "PASS", "controls": controls}, indent=2))
        return 0
    args.root = args.root.resolve()
    if args.root != ROOT:
        parser.error("this result recipe reviews its owning repository only")
    report = review(args)
    output = args.output.resolve()
    if output.parent != HERE or output.name != "relocation-review.json":
        parser.error("output must be this result directory's relocation-review.json")
    output.write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n")
    print(json.dumps({"status": report["status"], "output": str(output),
                      "source_before": report["baseline_source"]["worktree_sha256"],
                      "source_current": report["current_source_after"]["worktree_sha256"],
                      "problems": report["problems"], "files": len(report["files"]),
                      "controls": len(report["negative_and_positive_controls"])}, indent=2))
    return 0 if report["status"] == "PASS" else 1


if __name__ == "__main__":
    raise SystemExit(main())
