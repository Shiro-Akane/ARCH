#!/usr/bin/env python3
"""Read-only Git/index preservation and compiled-source identity check."""
import hashlib
import json
from pathlib import Path
import subprocess

OPTIMIZED = "4742e8dad21b7728ae68391fefd65282a5f231eb"
MAIN = "e12b96c15e022691d788f4b67faba4dc06da342e"
BUILT_TREE = "723e51b302edae758293664f3c1d9d26a5218694"


def git(*args):
    return subprocess.check_output(["git", *args])


def inventory(ref):
    result = {}
    for record in git("ls-tree", "-r", "-z", ref).split(b"\0"):
        if record:
            header, name = record.split(b"\t", 1)
            result[name.decode()] = header.decode().split()[2]
    return result


def main():
    candidate = git("write-tree").decode().strip()
    current, old, optimized, built = map(inventory, (candidate, MAIN, OPTIMIZED, BUILT_TREE))
    evidence = [name for name in optimized if name.startswith("validation/")]
    assert evidence and all(current.get(name) == optimized[name] for name in evidence), "optimization evidence changed"
    preserved_roots = ("benchmarks/", "datasets/", "experiments/", "docs/", "validation/",
                       "scripts/", ".codex-evidence/", ".superpowers/")
    unique = [name for name in old if name not in optimized and name.startswith(preserved_roots)]
    assert unique and all(current.get(name) == old[name] for name in unique), "personal artifacts changed or disappeared"
    build_paths = [name for name in built if name.startswith(("src/", "simulation/", "cmake/"))
                   or name in ("CMakeLists.txt", "CMakePresets.json", "tests/host/test_predictive_amr_recorder.cpp")]
    normalized = []
    for name in build_paths:
        assert name in current, name
        if built[name] == current[name]:
            continue
        before = git("cat-file", "blob", built[name])
        after = git("cat-file", "blob", current[name])
        assert before.replace(b"\r\n", b"\n") == after.replace(b"\r\n", b"\n"), name
        normalized.append(name)
    print(json.dumps({"status": "passed", "candidate_tree": candidate,
        "previous_main": MAIN, "optimized": OPTIMIZED, "compiled_tree": BUILT_TREE,
        "optimization_evidence_files_byte_preserved": len(evidence),
        "main_only_artifact_files_byte_preserved": len(unique),
        "compiled_source_files_checked": len(build_paths), "crlf_to_lf_only": normalized,
        "gpu_qualification_added": False,
        "recipe_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest()}, indent=2))


if __name__ == "__main__":
    main()
