#!/usr/bin/env python3
"""Bounded CPU static-state integration and startup guards for completed EOS.

Run with an environment providing h5py/numpy. Example table arguments are
EOS_toolkit/tables/baryon/eos2.tab and eos4.tab. This checks a uniform 16-cell,
three-step application trajectory, not nonuniform nuclear-matter accuracy.
Source-node P/E accuracy belongs to BaryonEosRegression, not this replay.
"""
import argparse
import json
import os
from pathlib import Path
import sys
from datetime import datetime, timezone

import h5py
import numpy as np

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / "tools"))
import validate_backend_results as runtime
import validation_provenance as provenance

BASE = ROOT / "simulation/Cellular/Cellular.par"
TEMPERATURE_BUDGET = 2e-8
CLOSURE_BUDGET = 2e-12
SPECIES_BUDGET = 1e-12
STEPS, CELLS = 3, 16
STATIC = dict(
    geometry="cartesian", nblockx1="1", nblockx2="0", nblockx3="0", max_blocks="4",
    x1_min="0", x1_max="1e8", x2_min="0", x2_max="1", x3_min="0", x3_max="1",
    x1l_boundary_type="periodic", x1r_boundary_type="periodic",
    x2l_boundary_type="outflow", x2r_boundary_type="outflow",
    x3l_boundary_type="outflow", x3r_boundary_type="outflow",
    solver="HLLC", reconstruct="muscl", limiter="mc", time_integrator="RK2", cfl=".25",
    sml_rho="1e-12", min_eint="1e-10", max_eint="1e25",
    gravity_type="none", use_diffusion="false", use_thermal_diff="false", alpha_therm="0",
    use_burn="false", use_nse="false", network_name="aprox19", ode_solver="ROS4",
    linear_solver="DenseLU", shock_dir="0", radiusPerturb="5e7",
    rhoAmbient="1e10", tempAmbient="1e10", rhoPerturb="1e10", tempPerturb="1e10",
    velxPerturb="0", noiseAmplitude="0", xneut=".7", xprot=".3",
    xhe4="0", xc12="0", xo16="0", eos_type="tabular", plt_dt="-1", plt_dstep="1",
    chk_dt="-1", chk_dstep="-1",
    plt_variables="DENS,TEMP,PRES,VELX,VELY,VELZ,ENER,SPECIES")
GUARDS = {
    "burn": (dict(use_burn="true"), "Independent kinetic burning/NSE would double-count nuclear energy"),
    "sw": (dict(solver="SW"), "Steger-Warming requires a composition-only gamma"),
    "automatic-thermal": (dict(use_diffusion="true", use_thermal_diff="true", alpha_therm="0"),
                          "does not supply the electron diagnostics required by automatic stellar conductivity"),
}


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def native_fields(path):
    with h5py.File(path) as source:
        return {name: source["Data"][name][:] for name in source["Data"]}


def exact_uniform(fields, name):
    for field, values in fields.items():
        require(np.all(np.isfinite(values)), f"{name}/{field} has nonfinite data")
        cells = values.reshape(values.shape[0], -1) if field in ("X", "rhoX") else values.reshape(1, -1)
        require(np.all(cells == cells[:, :1]), f"{name}/{field} is not spatially uniform")


def exact_equal(first, last, name):
    require(first.keys() == last.keys(), f"{name} field inventory changed")
    for field in first:
        require(first[field].dtype == last[field].dtype and first[field].shape == last[field].shape
                and first[field].tobytes() == last[field].tobytes(),
                f"{name}/{field} changed during static evolution")


def inspect_static(lane):
    plots = sorted(lane.glob("*_plt_*.h5"))
    checkpoints = sorted(lane.glob("*_chk_*.h5"))
    require(len(plots) == STEPS + 1 and len(checkpoints) == 2, "unexpected static output inventory")
    initial, final = native_fields(checkpoints[0]), native_fields(checkpoints[-1])
    exact_uniform(initial, "initial checkpoint")
    exact_uniform(final, "final checkpoint")
    exact_equal(initial, final, "native conserved state")
    with h5py.File(checkpoints[0]) as first, h5py.File(checkpoints[-1]) as last:
        require(int(first.attrs["step"]) == 0 and int(last.attrs["step"]) == STEPS, "checkpoint step count changed")
        require(int(last.attrs["cells_per_block"]) == CELLS and final["rho"].size == CELLS,
                "static replay was not one 16-cell block")
        require(float(last.attrs["time"]) > 0, "static replay did not advance time")
        require(first["Blocks"].keys() == last["Blocks"].keys(), "topology inventory changed")
        for field in first["Blocks"]:
            require(np.array_equal(first["Blocks"][field][:], last["Blocks"][field][:]), "static topology changed")
        A, Z = last["Species/A"][:], last["Species/Z"][:]
        terminal_time = float(last.attrs["time"])
    require(np.all(final["rho"] == 1e10), "density differs from prescribed static state")
    for field in ("mom_u", "mom_v", "mom_w", "enuc_rate"):
        require(np.all(final[field] == 0), f"unexpected static {field}")
    species_sum = float(np.max(np.abs(np.sum(final["X"], axis=0) - 1)))
    ye = np.sum(final["X"] * (Z/A).reshape((-1,) + (1,) * (final["X"].ndim-1)), axis=0)
    ye_error = float(np.max(np.abs(ye - .3)))
    conserved_species = final["X"] * final["rho"]
    rhoX_error = float(np.max(np.abs(final["rhoX"]-conserved_species) / np.maximum(np.abs(conserved_species), 1)))
    require(species_sum <= SPECIES_BUDGET and ye_error <= SPECIES_BUDGET, "composition/charge closure failed")
    require(rhoX_error <= CLOSURE_BUDGET, "conserved-species closure failed")
    first_plot = native_fields(plots[0])
    for plot in plots:
        fields = native_fields(plot)
        exact_uniform(fields, plot.name)
        exact_equal(first_plot, fields, "plotted thermodynamics/species")
    require(np.array_equal(first_plot["DENS"], final["rho"]), "plot/checkpoint density differs")
    energy_error = float(np.max(np.abs(first_plot["ENER"]-final["eng"]) / np.maximum(np.abs(final["eng"]), 1)))
    require(energy_error <= CLOSURE_BUDGET, "plot/conserved-energy closure failed")
    temperature_error = float(np.max(np.abs(first_plot["TEMP"]/1e10 - 1)))
    require(temperature_error <= TEMPERATURE_BUDGET, "primitive/conserved temperature inverse failed")
    require(np.all(first_plot["PRES"] > 0) and np.all(final["eng"] > 0), "nonpositive thermodynamic state")
    return dict(steps=STEPS, cells=CELLS, native_fields=len(final), plot_fields=len(first_plot),
        exact_spatial_uniformity=True, exact_initial_final_state=True, final_time=terminal_time,
        temperature_relative_error=temperature_error, energy_closure_relative=energy_error,
        rhoX_closure_relative=rhoX_error, species_sum_error=species_sum, Ye_absolute_error=ye_error,
        pressure=float(first_plot["PRES"].flat[0]), energy_density=float(final["eng"].flat[0]),
        artifacts=[provenance.file_identity(path) for path in plots + checkpoints])


def run_case(arch, output, table, helm, name, guard=None, initial_state=None):
    lane = output / name
    lane.mkdir()
    parameter = lane / "static.par"
    overrides = dict(STATIC, eos_table_path=str(table), eos_helm_table_path=str(helm))
    if initial_state:
        overrides.update(initial_state)
    if guard:
        overrides.update(GUARDS[guard][0])
    runtime.render_parameter_file(BASE, parameter, backend="cpu", output_dir=lane,
        base_name="Static", accepted_steps=STEPS, scientific_overrides=overrides)
    input_identity = provenance.file_identity(parameter)
    completed = runtime.run_arch_with_logs([str(arch), "CellularDet", str(parameter)],
        source_root=ROOT, lane_root=lane, timeout=300)
    require(provenance.file_identity(parameter) == input_identity, "runtime parameter input changed")
    transcript = completed.stdout + "\n" + completed.stderr
    record = dict(id=name, parameter=input_identity, table=str(table), returncode=completed.returncode,
        stdout=provenance.file_identity(lane / "arch.stdout"), stderr=provenance.file_identity(lane / "arch.stderr"))
    if guard:
        message = GUARDS[guard][1]
        require(completed.returncode != 0 and message in transcript, f"{name}: expected startup rejection absent")
        require("Initializing Root Grid" not in transcript and "Simulation Done" not in transcript,
                f"{name}: rejected only after evolution startup")
        require(not list(lane.glob("*_plt_*.h5")) and not list(lane.glob("*_chk_*.h5")),
                f"{name}: startup rejection produced state files")
        record.update(startup_rejection=guard, expected_message=message)
    else:
        require(completed.returncode == 0, f"{name}: ARCH failed; inspect retained stdout/stderr")
        match = runtime.STEP_RE.search(completed.stdout)
        require(match is not None and int(match.group(1)) == STEPS, "accepted-step count differs from three")
        record["plan"] = runtime.validate_resolved_plan(lane / "Static_backend_plan.txt", "cpu",
            dict(eos="tabular3d", flux="hllc", network="none", diffusion="none"))
        record["static_checks"] = inspect_static(lane)
    print(f"PASS {name}", flush=True)
    return record


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--arch", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--helm-table", type=Path, required=True)
    parser.add_argument("--table", type=Path, action="append", required=True)
    parser.add_argument("--completed-table", type=Path)
    args = parser.parse_args()
    arch, output, helm = args.arch.resolve(), args.output_dir.resolve(), args.helm_table.resolve()
    tables = [table.resolve() for table in args.table]
    normalized = args.completed_table.resolve() if args.completed_table else None
    runtime.require_empty_output_root(output)
    output.mkdir(parents=True, exist_ok=True)
    os.environ["OMP_NUM_THREADS"], os.environ["OMP_DYNAMIC"] = "2", "FALSE"
    dependencies = [arch, helm, BASE, Path(__file__).resolve(), *tables, *([normalized] if normalized else [])]
    identities = [provenance.file_identity(path) for path in dependencies]
    evidence = dict(schema=1, scope="bounded uniform CPU EOS integration/startup guards; not nonuniform physical accuracy",
        passed=False, release_qualified=False, dependencies=identities,
        binary_before=identities[0], helm_table_before=identities[1],
        environment=provenance.execution_environment_identity(), started_utc=datetime.now(timezone.utc).isoformat(),
        budgets=dict(temperature_relative=TEMPERATURE_BUDGET, closure_relative=CLOSURE_BUDGET,
                     composition_absolute=SPECIES_BUDGET, state_change="exact zero"), cases=[])
    try:
        for index, table in enumerate(tables):
            prefix = f"raw-{index+1}-{table.stem}"
            evidence["cases"].append(run_case(arch, output, table, helm, prefix + "-static"))
            for guard in GUARDS:
                evidence["cases"].append(run_case(arch, output, table, helm, prefix + "-reject-" + guard, guard))
        if normalized:
            # Problem construction queries the EOS before solver dispatch.
            # Use the manufactured table's interior, so these negative controls
            # reach the coupling check rather than failing on an unrelated domain.
            with h5py.File(normalized) as source:
                rho = 10**(.5*(float(source['log_rho_min'][()])+float(source['log_rho_max'][()])))
                temperature = 10**(.5*(float(source['log_T_min'][()])+float(source['log_T_max'][()])))
                ye = .5*(float(source['X_min'][()])+float(source['X_max'][()]))
            initial_state = dict(rhoAmbient=str(rho),rhoPerturb=str(rho),
                tempAmbient=str(temperature),tempPerturb=str(temperature),
                xprot=str(ye),xneut=str(1-ye))
            for guard in ("sw", "automatic-thermal"):
                evidence["cases"].append(run_case(arch, output, normalized, helm,
                    "normalized-reject-" + guard, guard, initial_state))
        observed = [provenance.file_identity(path) for path in dependencies]
        evidence.update(dependencies_after=observed, binary_after=observed[0], helm_table_after=observed[1])
        require(identities == observed, "binary/recipe/EOS inputs changed")
        evidence.update(passed=True, static_cases=len(tables), rejection_cases=3*len(tables)+(2 if normalized else 0),
            explicit_positive_alpha=dict(executed=False, model="constant diffusivity; alpha_therm > 0"))
    except Exception as error:
        evidence["failure"] = str(error)
        raise
    finally:
        evidence["finished_utc"] = datetime.now(timezone.utc).isoformat()
        (output / "evidence.json").write_text(json.dumps(evidence, indent=2) + "\n", encoding="utf-8")
    print("bounded static EOS replay PASS: " + str(output / "evidence.json"))


if __name__ == "__main__":
    main()
