"""Profile validated CUDA lanes from the original Sedov timing report.

This is a separate diagnostic run: its timings never enter baseline medians.
Only output paths and LD_PRELOAD differ from the selected baseline lane.
"""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path, required=True)
    parser.add_argument("--baseline", type=Path, required=True)
    parser.add_argument("--observer", type=Path, required=True)
    parser.add_argument("--output-root", type=Path, required=True)
    parser.add_argument("--blocks", type=int, nargs="+", default=[4, 8])
    args = parser.parse_args()
    root = args.source_root.resolve()
    sys.path.insert(0, str(root / "tools"))
    import validate_backend_results as validation
    import validation_provenance as provenance

    baseline = json.loads(args.baseline.read_text())
    if baseline["status"] != "passed":
        raise RuntimeError("baseline must pass its numerical and workload checks")
    output = args.output_root.resolve()
    validation.require_empty_output_root(output)
    output.mkdir(parents=True, exist_ok=True)
    report = {"status": "running", "baseline": provenance.file_identity(args.baseline),
              "observer": provenance.file_identity(args.observer), "lanes": []}
    try:
        for blocks in args.blocks:
            reference = next(l for l in baseline["lanes"] if l["backend"] == "cuda"
                             and l["blocks_per_axis"] == blocks and l["phase"] == "measured")
            directory = output / f"blocks-{blocks}"
            directory.mkdir()
            original = Path(reference["parameter"]["path"])
            parameter = directory / "run.par"
            parameter.write_text(original.read_text().replace(reference["directory"], str(directory)))
            old_map, new_map = (validation.read_parameter_map(p) for p in (original, parameter))
            for key in set(old_map) | set(new_map):
                if key not in ("out_dir", "log_dir") and old_map.get(key) != new_map.get(key):
                    raise RuntimeError(f"scientific parameter changed: {key}")
            environment = dict(os.environ, **baseline["environment"],
                               LD_PRELOAD=str(args.observer.resolve()),
                               ARCH_CUDA_API_PROFILE=str(directory / "api.json"))
            command = [baseline["settings"]["arch"], "Sedov", str(parameter)]
            with (directory / "arch.stdout").open("w") as stdout, (directory / "arch.stderr").open("w") as stderr:
                begin = time.perf_counter()
                result = subprocess.run(command, cwd=root, env=environment,
                                        stdout=stdout, stderr=stderr, timeout=1200)
                elapsed = time.perf_counter() - begin
            lane = {"blocks_per_axis": blocks, "command": command, "seconds": elapsed,
                    "returncode": result.returncode, "directory": str(directory)}
            report["lanes"].append(lane)
            result.check_returncode()
            lane["api"] = json.loads((directory / "api.json").read_text())
            if not any(a["calls"] for a in lane["api"]["apis"]):
                raise RuntimeError("no CUDA APIs intercepted")
            final = directory / "SedovTiming_chk_0001.h5"
            lane["comparison"] = validation.compare_hdf5_checkpoints(
                Path(reference["checkpoint"]), final, baseline["field_policy"],
                Path(baseline["settings"]["checkpoint_validator"]),
                comparison_mode="physical-time", target_time=baseline["settings"]["physical_time"])
            if not lane["comparison"]["passed"]:
                raise RuntimeError("profiled result differs from baseline")
        report["status"] = "passed"
    except Exception as error:
        report["status"], report["error"] = "failed", f"{type(error).__name__}: {error}"
    finally:
        (output / "profile-report.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))
    return 0 if report["status"] == "passed" else 1


if __name__ == "__main__":
    raise SystemExit(main())
