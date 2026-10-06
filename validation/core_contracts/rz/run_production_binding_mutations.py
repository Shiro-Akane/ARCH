"""Compile private RZ owner/executor mutations; restore exact source in finally."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[3]

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    source, build, output = args.source.resolve(), args.build.resolve(), args.output.resolve()
    owned = (ROOT / "studio/.local/integration").resolve()
    if any(not path.is_relative_to(owned) for path in (source, build, output)) or output.exists():
        raise RuntimeError("private source/build and fresh owned output required")
    cache = (build / "CMakeCache.txt").read_text()
    if "CMAKE_HOME_DIRECTORY:INTERNAL=" + str(source) not in cache:
        raise RuntimeError("build/source association mismatch")
    output.mkdir(parents=True)
    elf = build / "arch_curvilinear_metrics"
    original_elf = sha(elf)
    shutil.copy2(elf, output / "original-test-elf")
    mutations = [
        ("missing_numerical_rollback", "src/numerics/integrator/HydroGeometryBinding.h",
         "if(!context_||std::uncaught_exceptions()==exceptions_)return;", "if(true)return;",
         "rz-source-rejection-audit"),
        ("PCM_wrong_source_limiter", "src/numerics/reconstruction/Reconstruction.h",
         "using SourceLimiter = NoLimiter;", "using SourceLimiter = MinMod;",
         "rz-source-owner-audit"),
        ("duplicate_cache_consumption", "src/physics/gravity/ExternalGravity.h",
         "if(entry.consumed->exchange(true))", "if(false)", "rz-source-owner-audit"),
    ]
    records = []
    def rebuild(label):
        guard_log = output / (label + "-build.log")
        cmd = ["python3", str(ROOT / "tools/run_memory_guarded.py"),
               "--min-available-mib", "4096", "--max-swap-growth-mib", "256",
               "--log", str(guard_log), "--", "timeout", "--signal=TERM",
               "--kill-after=10", "1800", "cmake", "--build", str(build),
               "--target", "arch_curvilinear_metrics", "--parallel", "28"]
        result = subprocess.run(cmd, capture_output=True, text=True, timeout=1820)
        (output / (label + "-guard.log")).write_text(result.stdout + result.stderr)
        if result.returncode:
            raise RuntimeError(label + ": compile failure is NOT mutation rejection")
    try:
        for name, relative, old, new, mode in mutations:
            path = source / relative
            original = path.read_bytes()
            text = original.decode()
            if text.count(old) != 1:
                raise RuntimeError(name + ": candidate pattern changed")
            try:
                path.write_text(text.replace(old, new))
                mutant_source_sha = sha(path)
                rebuild(name)
                mutant_elf_sha = sha(elf)
                shutil.copy2(elf, output / (name + "-elf"))
                result = subprocess.run([str(elf), mode], capture_output=True, text=True,
                    timeout=120, env={**os.environ, "OMP_NUM_THREADS": "1"})
                (output / (name + "-test.log")).write_text(result.stdout + result.stderr)
                if result.returncode == 0:
                    raise RuntimeError(name + ": incorrect implementation falsely passed")
                records.append({"name": name, "compiled": True, "exit_code": result.returncode,
                    "mutant_source_sha256": mutant_source_sha, "elf_sha256": mutant_elf_sha,
                    "error": result.stderr.strip()})
                print(name + " compiled=PASS rejected=PASS", flush=True)
            finally:
                path.write_bytes(original)
    finally:
        rebuild("restored")
    restored = sha(elf)
    if restored != original_elf:
        raise RuntimeError("restored ELF identity changed; repeat affected checks before using")
    (output / "summary.json").write_text(json.dumps({
        "status": "PASS_PRIVATE_OWNER_EXECUTOR_MUTATION_SENSITIVITY",
        "original_elf_sha256": original_elf, "restored_elf_sha256": restored,
        "rejected_mutations": records}, indent=2) + "\n")
    print("exact_source_and_ELF_restored=PASS", flush=True)

if __name__ == "__main__":
    main()
