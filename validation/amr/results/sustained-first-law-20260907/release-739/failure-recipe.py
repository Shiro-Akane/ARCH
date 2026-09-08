"""Bounded sustained runs derived from the canonical AMR/restart inputs.

Run under tools/run_memory_guarded.py with host and GPU memory observation.
The existing validators own execution, checkpoint interpretation, conservation,
parity and restart policy. This recipe only selects longer work and chains
ordinary intermediate checkpoints; it never changes checkpoint contents.
"""
import argparse
from copy import deepcopy
from datetime import datetime, timezone
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / "tools"))
import validate_backend_results as runtime
import validate_cuda_amr_restart as restart
import validation_provenance as provenance


def restart_chain(arch, validator, output, problem, input_path, cycles):
    total = 2 * (cycles + 1)
    def run(name, backend, steps, restored=None):
        arguments = {} if restored is None else dict(
            restart_file=restored[0], restart_parameters=restored[1],
            restart_step=restored[2], restart_phase=True)
        lane = restart.run_lane(arch=arch, source_root=ROOT, canonical_input=input_path,
            output_root=output, name=name, problem=problem, backend=backend,
            max_steps=steps, checkpoint_validator=validator, **arguments)
        values = runtime.read_parameter_map(Path(lane["parameter"]))
        directory = Path(values.get("log_dir", values["out_dir"]))
        lane["regrid"] = runtime.read_regrid_metrics(
            directory / (values["base_name"] + "_regrid.tsv"), backend, steps)
        return lane

    references = {backend: run("continuous_" + backend, backend, total)
                  for backend in ("cpu", "cuda")}
    comparisons = [restart.compare(validator, references["cpu"], references["cuda"])]
    source = run("segment_0", "cpu", 3)
    segments, selected = [source], []
    for index in range(1, cycles + 1):
        step = 2 * index
        parameter = Path(source["parameter"])
        checkpoints = [(path, runtime.checkpoint_metadata(validator=validator,
            checkpoint=path, parameters=parameter, expected_steps=None))
            for path in parameter.parent.glob("*_chk_*.h5")]
        checkpoint, metadata = restart.select_checkpoint(checkpoints, step, True)
        selected.append({"checkpoint": str(checkpoint), "parameter": str(parameter), **metadata})
        backend = "cuda" if index % 2 else "cpu"
        final = index == cycles
        source = run(f"segment_{index}", backend, total if final else step + 3,
                     (checkpoint, parameter, step))
        segments.append(source)
    comparisons.append(restart.compare(validator, references[source["backend"]], source))
    return dict(problem=problem, cycles=cycles, total_steps=total,
                references=references, segments=segments, selected_checkpoints=selected,
                comparisons=comparisons)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--restart-cycles", type=int, default=12)
    args = parser.parse_args()
    if args.restart_cycles < 4:
        parser.error("sustained restart profile requires at least four chained restores")
    build, output = args.build_dir.resolve(), args.output_dir.resolve()
    arch, validator = build / "bin/ARCH", build / "arch_cuda_single_level_validation"
    runtime.require_empty_output_root(output)
    output.mkdir(parents=True, exist_ok=True)
    identity_args = dict(arch=arch, checkpoint_validator=validator, source_root=ROOT, build_dir=build)
    identity = provenance.capture(**identity_args)
    recipe = provenance.file_identity(Path(__file__).resolve())
    manifest_path = ROOT / "validation/amr/gpu_cases.json"
    manifest_identity = provenance.file_identity(manifest_path)
    inventory = {case["id"]: case for case in runtime.load_manifest(manifest_path)["cases"]}
    cases = []
    for name, extra_steps in (("hydro_amr_regrid_cycle_1d", (100, 500)),
                              ("diffusion_amr_rkl2_5stage", (25, 100))):
        case = deepcopy(inventory[name])
        case["accepted_steps"] = sorted(set(case["accepted_steps"]) | set(extra_steps))
        cases.append(case)
    inputs = runtime.runtime_case_inputs(cases, ROOT)
    restart_cases = (("SmoothAdvection", ROOT / "validation/amr/inputs/smooth_amr80_l1.par"),
                     ("BurnGradient", ROOT / "validation/amr/inputs/burn_enuc_amr.par"))
    def restart_inputs():
        return {problem: provenance.runtime_inputs(parameter_file=path, working_directory=ROOT,
            parameter_reader=runtime.read_parameter_map) for problem, path in restart_cases}
    restart_input_identity = restart_inputs()
    started = datetime.now(timezone.utc).isoformat()
    records = []
    for case in cases:
        record = runtime.run_case(arch, validator, ROOT, case, output / "matrices")
        last = record["checkpoints"][-1]["cuda"]["regrid"]["summary"]
        if last["records"] < max(case["accepted_steps"]):
            raise RuntimeError("sustained case did not execute each requested regrid")
        records.append(record)
        print(f"sustained PASS: {case['id']}", flush=True)
    chains = []
    for problem, path in restart_cases:
        chains.append(restart_chain(arch, validator, output / problem, problem,
            path, args.restart_cycles))
        print(f"chained restart PASS: {problem}", flush=True)
    if recipe != provenance.file_identity(Path(__file__).resolve()) \
            or manifest_identity != provenance.file_identity(manifest_path) \
            or inputs != runtime.runtime_case_inputs(cases, ROOT) \
            or restart_input_identity != restart_inputs():
        raise RuntimeError("sustained recipe or canonical inputs changed during execution")
    evidence = dict(schema=1, scope="bounded-sustained-amr-and-chained-restart",
        release_qualified=False, focused_gate_pass=True, recipe=recipe,
        manifest=manifest_identity, derived_cases=cases, runtime_inputs=inputs,
        restart_inputs=restart_input_identity,
        started_utc=started, finished_utc=datetime.now(timezone.utc).isoformat(),
        cases=records, restart_chains=chains)
    provenance.write_evidence(output / "evidence.json", evidence, identity, **identity_args)
    print(f"sustained profile PASS: {output / 'evidence.json'}")


if __name__ == "__main__":
    main()
