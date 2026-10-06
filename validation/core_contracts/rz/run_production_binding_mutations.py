"""Compile private RZ owner/executor mutations; restore exact source unconditionally.

Frozen contract: CORE-RZ-20261006-v1 / runner-hardening-v1.

A mutation is only rejected when ALL of the following hold:
  * the mutant run exits with exactly 1 (never 0, never another code);
  * it was not terminated by a signal and did not time out;
  * the rebuilt mutant ELF differs from the original ELF;
  * the mutation-specific diagnostic below is observed.
Signals, arbitrary failures, timeouts and compile failures NEVER pass.

The identity preflight helper is the same one used by the closure runner: the
declared ``--identity`` JSON must match the executable, the grid fixture and the
inert candidate patch before anything is compiled or launched.

Cleanup (byte-exact source restoration, rebuild, restored-ELF digest check and
identity re-validation) runs unconditionally after the mutation loop, including
when a mutation check raises, and any cleanup failure is chained under the
original failure reason instead of replacing it.
"""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[3]
MUTATION_EXPECTATIONS = {
    "missing_numerical_rollback":
        "actual numerical rejection corrupted state/ledger/clock/flux/source budget",
    "PCM_wrong_source_limiter": "actual PCM source limiter",
    "duplicate_cache_consumption": "invalid production/cache identity accepted",
}
MUTATIONS = (
    ("missing_numerical_rollback", "src/numerics/integrator/HydroGeometryBinding.h",
     "if(!context_||std::uncaught_exceptions()==exceptions_)return;", "if(true)return;",
     "rz-source-rejection-audit"),
    ("PCM_wrong_source_limiter", "src/numerics/reconstruction/Reconstruction.h",
     "using SourceLimiter = NoLimiter;", "using SourceLimiter = MinMod;",
     "rz-source-owner-audit"),
    ("duplicate_cache_consumption", "src/physics/gravity/ExternalGravity.h",
     "if(entry.consumed->exchange(true))", "if(false)", "rz-source-owner-audit"),
)


def _load_closure_contract():
    path = Path(__file__).with_name("run_frozen_rz_closure.py")
    spec = importlib.util.spec_from_file_location("rz_frozen_closure_contract", path)
    if spec is None or spec.loader is None:
        raise RuntimeError("closure contract module cannot be loaded: " + str(path))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


closure = _load_closure_contract()


def positive_int(text):
    try:
        value = int(text)
    except (TypeError, ValueError):
        raise argparse.ArgumentTypeError("expected a positive integer, got " + repr(text)) from None
    if value < 1:
        raise argparse.ArgumentTypeError("expected a positive integer, got " + repr(text))
    return value


def mutation_record(name, exit_code, output_text, original_elf_sha, mutant_elf_sha,
                    mutant_source_sha256=None, timed_out=False):
    """Validate one mutant run against the frozen rejection contract."""
    if name not in MUTATION_EXPECTATIONS:
        raise closure.ContractError("unknown mutation " + repr(name))
    expected = MUTATION_EXPECTATIONS[name]
    if timed_out:
        raise closure.ContractError(name + ": timeout is NOT mutation rejection")
    if exit_code is None:
        raise closure.ContractError(name + ": missing exit status")
    if exit_code < 0:
        raise closure.ContractError(name + ": terminated by signal " + str(-exit_code)
                                    + "; a signal is NOT mutation rejection")
    if exit_code == 0:
        raise closure.ContractError(name + ": incorrect implementation falsely passed")
    if exit_code != 1:
        raise closure.ContractError(name + ": expected exact exit 1, got " + str(exit_code)
                                    + "; an unrelated failure is NOT mutation rejection")
    if mutant_elf_sha == original_elf_sha:
        raise closure.ContractError(name + ": mutant ELF is identical to the original ELF")
    if expected not in output_text:
        raise closure.ContractError(name + ": expected diagnostic " + repr(expected)
                                    + " was not observed")
    return {"name": name, "compiled": True, "exit_code": 1,
            "expected_diagnostic": expected, "mutant_source_sha256": mutant_source_sha256,
            "mutant_elf_sha256": mutant_elf_sha, "error": output_text.strip()}


def rebuild_mutant(build, output, jobs, label):
    """Rebuild the mutation target under the unchanged resource guard."""
    guard_log = output / (label + "-build.log")
    cmd = ["python3", str(ROOT / "tools/run_memory_guarded.py"),
           "--min-available-mib", "4096", "--max-swap-growth-mib", "256",
           "--log", str(guard_log), "--", "timeout", "--signal=TERM",
           "--kill-after=10", "1800", "cmake", "--build", str(build),
           "--target", "arch_curvilinear_metrics", "--parallel", str(jobs)]
    result = subprocess.run(cmd, capture_output=True, text=True, timeout=1820)
    (output / (label + "-guard.log")).write_text(result.stdout + result.stderr)
    if result.returncode:
        raise RuntimeError(label + ": compile failure is NOT mutation rejection")


def restore_and_verify(original_sources, paths, build, output, jobs, identity, original_elf):
    """Unconditionally restore source bytes, rebuild, and re-validate identities.

    Every step runs even when an earlier cleanup step fails, so a failed
    mutation check can never skip the exact-restoration / ELF / identity
    validation that the frozen contract requires. Problems are returned as
    messages instead of being raised, so callers can chain them under the
    original failure reason.
    """
    errors = []
    for path, original in original_sources.items():
        try:
            Path(path).write_bytes(original)
        except Exception as error:
            errors.append("source restore failed for " + str(path) + ": " + repr(error))
    try:
        rebuild_mutant(build, output, jobs, "restored")
    except Exception as error:
        errors.append("restored rebuild failed: " + repr(error))
    try:
        restored = closure.digest(paths["elf"])
        if restored != original_elf:
            raise RuntimeError("restored ELF identity changed; "
                               "repeat affected checks before using")
    except Exception as error:
        errors.append("restored ELF check failed: " + repr(error))
    try:
        closure.verify_identity(identity, paths, "restored")
    except Exception as error:
        errors.append("restored identity check failed: " + repr(error))
    return errors


def run_mutations(source, build, output, jobs, identity, paths):
    """Run every mutation, then always restore the source and re-verify it."""
    elf = paths["elf"]
    original_elf = closure.digest(elf)
    shutil.copy2(elf, output / "original-test-elf")
    records = []
    original_sources = {}
    failure = None
    try:
        for name, relative, old, new, mode in MUTATIONS:
            path = source / relative
            original = path.read_bytes()
            original_sources[str(path)] = original
            try:
                text = original.decode()
                if text.count(old) != 1:
                    raise RuntimeError(name + ": candidate pattern changed")
                path.write_text(text.replace(old, new))
                mutant_source_sha = closure.digest(path)
                rebuild_mutant(build, output, jobs, name)
                mutant_elf_sha = closure.digest(elf)
                shutil.copy2(elf, output / (name + "-elf"))
                try:
                    result = subprocess.run([str(elf), mode], capture_output=True, text=True,
                        timeout=120, env={**os.environ, "OMP_NUM_THREADS": "1"})
                    exit_code, timed_out = result.returncode, False
                    combined = result.stdout + result.stderr
                except subprocess.TimeoutExpired:
                    exit_code, timed_out, combined = None, True, ""
                (output / (name + "-test.log")).write_text(combined)
                records.append(mutation_record(name=name, exit_code=exit_code,
                    output_text=combined, original_elf_sha=original_elf,
                    mutant_elf_sha=mutant_elf_sha, mutant_source_sha256=mutant_source_sha,
                    timed_out=timed_out))
                print(name + " compiled=PASS rejected=PASS", flush=True)
            finally:
                path.write_bytes(original)
    except BaseException as error:  # remember it, but always clean up first
        failure = error
    cleanup_errors = restore_and_verify(original_sources, paths, build, output, jobs,
                                        identity, original_elf)
    if cleanup_errors:
        message = "restored source/ELF state cannot be trusted: " + "; ".join(cleanup_errors)
        if failure is not None:
            raise RuntimeError(message + "; original failure: " + repr(failure)) from failure
        raise RuntimeError(message)
    if failure is not None:
        raise failure
    return records, original_elf


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--identity", required=True,
                        help="JSON file or inline JSON with "
                             "elf_sha256/fixture_sha256/patch_sha256")
    parser.add_argument("--jobs", type=positive_int, default=2,
                        help="parallel build jobs (positive int, default 2)")
    args = parser.parse_args()
    source, build, output = args.source.resolve(), args.build.resolve(), args.output.resolve()
    owned = (ROOT / "studio/.local/integration").resolve()
    if any(not path.is_relative_to(owned) for path in (source, build, output)) or output.exists():
        raise RuntimeError("private source/build and fresh owned output required")
    cache = (build / "CMakeCache.txt").read_text()
    if "CMAKE_HOME_DIRECTORY:INTERNAL=" + str(source) not in cache:
        raise RuntimeError("build/source association mismatch")
    identity = closure.read_identity(args.identity)
    paths = closure.guarded_paths(source, build)
    closure.verify_identity(identity, paths, "preflight")
    output.mkdir(parents=True)
    records, original_elf = run_mutations(source, build, output, args.jobs, identity, paths)
    observed = closure.verify_identity(identity, paths, "restored")
    (output / "summary.json").write_text(json.dumps({
        "status": "PASS_PRIVATE_OWNER_EXECUTOR_MUTATION_SENSITIVITY",
        "identity_sha256": identity, "jobs": args.jobs,
        "original_elf_sha256": original_elf, "restored_elf_sha256": observed["elf_sha256"],
        "elf_sha256_revalidated": observed["elf_sha256"],
        "qualification_scope": dict(closure.QUALIFICATION_SCOPE),
        "rejected_mutations": records}, indent=2) + "\n")
    print("exact_source_and_ELF_restored=PASS", flush=True)


if __name__ == "__main__":
    main()
