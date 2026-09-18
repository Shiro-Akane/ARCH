#!/usr/bin/env python3
"""CPU/CUDA integration regression for the optional read-only AMR recorder."""
import argparse
import csv
import json
import os
from pathlib import Path
import re
import sys

import h5py
import numpy as np

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
import validate_backend_results as runtime
import validation_provenance as provenance


digest = provenance.sha256


def compare(left, right, exact):
    report = {"left": str(left), "right": str(right), "exact_required": exact, "datasets": {}}
    with h5py.File(left) as a, h5py.File(right) as b:
        names_a, names_b = [], []
        a.visititems(lambda name, obj: names_a.append(name) if isinstance(obj, h5py.Dataset) else None)
        b.visititems(lambda name, obj: names_b.append(name) if isinstance(obj, h5py.Dataset) else None)
        assert names_a == names_b and names_a, "checkpoint schema changed"
        for name in names_a:
            x, y = a[name][()], b[name][()]
            assert x.shape == y.shape and x.dtype == y.dtype, name
            equal = bool(np.array_equal(x, y))
            relative = 0.0
            if x.dtype.kind == "f":
                assert np.isfinite(x).all() and np.isfinite(y).all(), name
                relative = float(np.max(np.abs(x - y) / np.maximum(1.0, np.maximum(np.abs(x), np.abs(y))))) if x.size else 0.0
                assert equal if exact else relative <= 2e-10, (name, relative)
            else:
                assert equal, name
            report["datasets"][name] = {"array_equal": equal, "max_scaled_error": relative}
        for name in ("step", "time", "dim", "num_species", "checkpoint_version"):
            assert np.array_equal(a.attrs[name], b.attrs[name]), name
    return report


def run(arch, source, output, dimension, enabled, steps, restart=None, backend="cpu"):
    output.mkdir()
    values = dict(nblockx1=4 if dimension == 1 else 2,
        nblockx2=2 if dimension == 2 else (1 if dimension == 3 else 0),
        nblockx3=1 if dimension == 3 else 0, max_blocks=128,
        geometry="cartesian", x1_min=0, x1_max=1, x2_min=0, x2_max=1, x3_min=0, x3_max=1,
        solver="HLLC", reconstruct="pcm", time_integrator="Euler", cfl=0.35,
        EntropyFix="false", sml_rho=1e-12, max_eint=1e21, compute_backend=backend,
        lrefinemin=0, lrefinemax=1, regrid_interval=1, refine_var="DENS",
        refine_threshold=0.35, derefine_threshold=0.04,
        predictive_amr_record=str(enabled).lower(), predictive_amr_horizon=4, predictive_amr_history=4,
        tmax=0.08, max_steps=steps, restart="true" if restart else "false",
        out_dir=output, base_name="witness", plt_dt=-1, plt_dstep=-1, chk_dt=-1,
        chk_dstep=-1, plt_variables="DENS,PRES,ENER,SPECIES", eos_type="ideal", gamma=1.4,
        gravity_type="none", use_burn="false", use_diffusion="false", shape_type=0,
        shock_dir=dimension - 1, x_pos=0.35, rho_left=1.0, p_left=1.0, u_left=0.0,
        v_left=0.0, rho_right=0.125, p_right=0.1, u_right=0.0, v_right=0.0)
    for direction in range(1, 4):
        for side in ("l", "r"):
            values[f"x{direction}{side}_boundary_type"] = "outflow"
    # Maintained Sod is intentionally 1D; use the registered Cartesian blast
    # for 2D/3D instead of weakening its geometry validation.
    problem = "Sod" if dimension == 1 else "Sedov"
    if dimension > 1:
        values.update(refine_var="PRES", deposit_radius=0.1,
                      ambient_pressure=1.0, explosion_energy=0.1)
    if restart:
        values["restart_file"] = restart
    parameter = output / "input.par"
    parameter.write_text("".join(f"{key} = {value}\n" for key, value in values.items()))
    command = [str(arch), problem, str(parameter)]
    completed = runtime.run_arch_with_logs(command, source_root=source,
                                          lane_root=output, timeout=240)
    (output / "execution.json").write_text(json.dumps({"command": command,
        "returncode": completed.returncode, "input_sha256": digest(parameter),
        "binary_sha256": digest(arch), "OMP_NUM_THREADS": "1"}, indent=2) + "\n")
    assert completed.returncode == 0, output
    text = completed.stdout
    runtime.validate_resolved_plan(output / 'witness_backend_plan.txt', backend)
    match = re.search(r"Simulation Done\. Total Steps:\s*(\d+)", text)
    assert match and int(match.group(1)) == steps, output
    candidates = sorted(output.glob("witness_chk_*.h5"))
    assert candidates, output
    checkpoint = candidates[-1]
    with h5py.File(checkpoint) as file:
        assert int(file.attrs["step"]) == steps, checkpoint
    records = sorted(output.glob("witness_predictive_amr*"))
    if enabled:
        assert len(records) == 5, records
        manifest = json.loads((output / "witness_predictive_amr_manifest.json").read_text())
        assert manifest["schema_version"] == 2 and manifest["observer_authority"] == "read_only"
        with (output / "witness_predictive_amr_nodes.csv").open() as stream:
            nodes = list(csv.DictReader(stream))
        with (output / "witness_predictive_amr_events.csv").open() as stream:
            events = list(csv.DictReader(stream))
        assert nodes and events, "empty recorder"
        assert sum(int(row["node_rows"]) for row in events) == len(nodes)
        for row in nodes:
            assert int(row["balance_override"]) == (int(row["criterion_action"]) != int(row["balanced_action"]))
    else:
        assert not records and "[ADAPTIVE RUNTIME DATA]" not in text, "disabled observer produced output"
    return checkpoint


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--arch", type=Path, required=True)
    parser.add_argument("--baseline", type=Path, help="optional separately built reference executable")
    parser.add_argument("--backend", nargs="+", choices=("cpu", "cuda"), default=["cpu", "cuda"])
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--build-dir", type=Path, help="defaults to the parent of the executable's bin directory")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if not __debug__:
        parser.error("run without Python -O; validation assertions must remain enabled")
    args.arch, args.source, args.output = (p.resolve() for p in (args.arch, args.source, args.output))
    if args.baseline:
        args.baseline=args.baseline.resolve()
    os.environ.update(OMP_NUM_THREADS="1", OMP_DYNAMIC="FALSE", OPENBLAS_NUM_THREADS="1")
    identity_args=dict(artifacts={'arch':args.arch},source_root=args.source,
                       build_dir=(args.build_dir or args.arch.parent.parent).resolve())
    before=provenance.capture_focused(**identity_args)
    args.output.mkdir(exist_ok=False)
    report = {"status": "running", "backends": args.backend, "performance_qualification": False,
              "candidate_binary_sha256": digest(args.arch), "baseline_binary_sha256": digest(args.baseline) if args.baseline else None,
              "recipe_sha256": digest(Path(__file__)), "identity_before":before, "comparisons": []}
    try:
        for backend in args.backend:
            for dimension in (1, 2, 3):
                root = args.output / f"{backend}-dim{dimension}"
                root.mkdir()
                def lane(name, enabled, steps, restart=None, binary=args.arch):
                    return run(binary, args.source, root / name, dimension,
                               enabled, steps, restart, backend=backend)
                disabled = lane("disabled", False, 6)
                enabled = lane("enabled", True, 6)
                report["comparisons"].append(compare(disabled, enabled, True))
                if args.baseline:
                    baseline = lane("baseline", False, 6, binary=args.baseline)
                    report["comparisons"].append(compare(baseline, disabled, False))
                partial = lane("partial", True, 3)
                resumed = lane("resumed", True, 6, partial)
                report["comparisons"].append(compare(enabled, resumed, True))
                print(f"{backend} dimension {dimension}: observer toggle and split-run passed", flush=True)
        report["status"] = "passed"
    except Exception as error:
        report["status"] = "failed"
        report["error"] = repr(error)
        raise
    finally:
        try:
            provenance.require_unchanged(before,provenance.capture_focused(**identity_args))
            if args.baseline and digest(args.baseline)!=report['baseline_binary_sha256']:
                raise RuntimeError('baseline executable changed during validation')
            report['identity_verified_after_run']=True
        except Exception as error:
            report.update(status='failed',identity_error=repr(error))
        report["files"] = [{"path": str(path.relative_to(args.output)), "bytes": path.stat().st_size,
                            "sha256": digest(path)} for path in sorted(args.output.rglob("*")) if path.is_file()]
        (args.output / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    if report['status']!='passed':
        raise RuntimeError('recorder validation failed; see report.json')


if __name__ == "__main__":
    main()
