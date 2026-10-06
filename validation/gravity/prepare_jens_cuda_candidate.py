"""Prepare the approved private JENS candidate; never modify the public tree.
No configure/build/run. Candidate identity is base Git commit plus inert patch.
"""
import argparse
import hashlib
import io
import json
import os
from pathlib import Path
import subprocess
import tarfile

BASE = "ccfcef5bc60f4361a808dc1e83c020094ab50930"
PATCH = "validation/gravity/candidates/jens-cuda-20261006.patch"
PATHS = ["CMakeLists.txt", "CMakePresets.json", "cmake", "src", "simulation",
         "include", "tests", "tools", "validation", "LICENSE"]

def digest(path):
    h = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()

def public_identity(root, touched):
    paths = touched + ["build-cpu/CMakeCache.txt", "build-cuda/CMakeCache.txt",
                       "build-cpu/bin/ARCH", "build-cuda/bin/ARCH"]
    return {p: digest(root / p) for p in paths}

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--destination", type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    owned = (root / "studio/.local/integration").resolve()
    destination = args.destination.resolve()
    if not destination.is_relative_to(owned) or destination == owned:
        raise RuntimeError("candidate destination must be an isolated integration child")
    if destination.exists():
        raise RuntimeError("refuse to overwrite existing candidate")
    patch = root / PATCH
    touched = [line.removeprefix("+++ b/") for line in patch.read_text().splitlines()
               if line.startswith("+++ b/")]
    if not touched or any(not p.startswith(("src/", "tests/", "validation/")) for p in touched):
        raise RuntimeError("unexpected patch scope")
    for path in touched:
        original = subprocess.check_output(["git", "show", BASE + ":" + path], cwd=root)
        if hashlib.sha256(original).hexdigest() != digest(root / path):
            raise RuntimeError("public touched source differs from pinned base: " + path)
    before = public_identity(root, touched)
    archive = subprocess.check_output(["git", "archive", BASE, *PATHS], cwd=root)
    if len(archive) > 256 * 1024 * 1024:
        raise RuntimeError("unexpected archive size")
    source = destination / "source"
    source.mkdir(parents=True)
    with tarfile.open(fileobj=io.BytesIO(archive)) as stream:
        stream.extractall(source, filter="data")
    # Existing EOS data are referenced, not copied. No public cache/binary reuse.
    (source / "EOS_toolkit").symlink_to(root / "EOS_toolkit", target_is_directory=True)
    env = os.environ | {"GIT_CEILING_DIRECTORIES": str(destination)}
    subprocess.run(["git", "apply", "--no-index", "--check", str(patch)],
                   cwd=source, env=env, check=True)
    subprocess.run(["git", "apply", "--no-index", str(patch)],
                   cwd=source, env=env, check=True)
    after = public_identity(root, touched)
    if before != after:
        raise RuntimeError("public identity changed during candidate preparation")
    inventory = {str(p.relative_to(source)): digest(p)
                 for p in sorted(source.rglob("*"))
                 if p.is_file() and not p.is_symlink()}
    manifest = {
        "status": "PREPARED_NOT_BUILT_NOT_VALIDATED",
        "base_commit": BASE, "patch_path": PATCH, "patch_sha256": digest(patch),
        "delivery_commit": subprocess.check_output(["git", "rev-parse", "HEAD"],
                                                  cwd=root, text=True).strip(),
        "candidate_source_root": str(source),
        "candidate_has_git_worktree": False,
        "public_identity_before": before, "public_identity_after": after,
        "source_inventory_sha256": inventory,
        "eos_data_reference": str(root / "EOS_toolkit"),
        "cpu_build": str(destination / "build-cpu"),
        "cuda_build": str(destination / "build-cuda"),
        "scope": "private Cartesian explicit CUDA JENS verification; public gates unchanged",
        "contract": "uniform-lifecycle-1",
        "limits": {
            "compile_parallel_jobs": 28, "cuda_heavy_jobs": 1,
            "compile_timeout_seconds": 3600,
            "minimum_available_memory_mib": 4096,
            "maximum_swap_growth_mib": 256,
            "campaign_threads": 1, "per_run_timeout_seconds": 1200,
            "campaign_total_timeout_seconds": 14400,
            "device": "GPU0 RTX 4070 Ti",
            "raw_output_budget_mib": 10240,
            "vram_limit_mib": 10240
        },
        "limit_note": "Declared plan; preparation does not enforce run limits. Run wrapper must enforce before launch.",
        "acceptance": "CPU candidate scoped tests and frozen9+9 before CUDA9+9; full short pass still requires Core release review"
    }
    (destination / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(json.dumps({"manifest": str(destination / "manifest.json"),
                      "source": str(source), "base": BASE,
                      "patch_sha256": manifest["patch_sha256"],
                      "public_identity_unchanged": True}))
if __name__ == "__main__":
    main()
