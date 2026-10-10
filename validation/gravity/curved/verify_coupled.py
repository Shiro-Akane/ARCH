"""Audit existing CPU four-module AMR runs without launching simulations.

Usage: verify_coupled.py --run label:directory:expected_steps [--run ...]
       verify_coupled.py --endpoint-run label:directory:physical_time [--endpoint-run ...]
                        [--evolving-amr]
At least one run of either form is required. The check reads the actual Driver
outputs, not a private case implementation.
"""

import argparse
import csv
import json
import math
import re
from pathlib import Path

import h5py
import numpy as np


def leaf_levels(plot, *, evolving_amr=False):
    """Return active levels; dynamic endpoints require a legal nonempty distribution."""
    with h5py.File(plot) as handle:
        levels = handle["Grid/level"][()]
        if evolving_amr and (levels.ndim != 1 or levels.dtype.kind not in "iu"
                             or levels.size == 0 or np.any(levels < 0)):
            raise ValueError(f"Invalid active AMR levels in {plot}")
        counts = {str(int(level)): int(count) for level, count in
                  zip(*np.unique(levels, return_counts=True))}
        time = float(handle.attrs["time"])
        fields = {}
        for name in ("DENS", "PRES", "TEMP", "ENER", "ENUC", "GPOT",
                     "GACX", "GACY", "c12", "o16"):
            values = handle[f"Data/{name}"][()]
            if not np.all(np.isfinite(values)):
                raise ValueError(f"Nonfinite {name} in {plot}")
            fields[name] = [float(values.min()), float(values.max())]
        if int(handle.attrs["dim"]) == 3:
            values = handle["Data/GACZ"][()]
            if not np.all(np.isfinite(values)):
                raise ValueError(f"Nonfinite GACZ in {plot}")
            fields["GACZ"] = [float(values.min()), float(values.max())]
    if fields["DENS"][0] <= 0 or fields["TEMP"][0] <= 0:
        raise ValueError(f"Nonpositive physical state in {plot}")
    return {"time_seconds": time, "leaves_by_level": counts,
            "field_min_max": fields}


def physical_times_agree(left, right):
    """Retain the existing coupled endpoint budget, rejecting invalid times."""
    if not all(math.isfinite(value) for value in (left, right)):
        return False
    scale = max(abs(left), abs(right), 1e-30)
    return abs(left - right) <= max(1e-20, 2e-10 * scale)


def verify(label, directory, expected_steps, expect_mixed=True, *, expected_time=None,
           evolving_amr=False):
    """Audit the original topology policy or opt-in evolving endpoint AMR."""
    if evolving_amr and (expected_time is None or not expect_mixed):
        raise ValueError(f"{label}: evolving AMR requires endpoint AMR mode")
    if expected_time is not None:
        if not math.isfinite(expected_time) or expected_time <= 0 or expected_steps is not None:
            raise ValueError(f"{label}: endpoint mode requires positive finite time and no step quota")
    elif expected_steps is None or expected_steps < 1:
        raise ValueError(f"{label}: positive expected steps required")
    plots = sorted(directory.glob("*_plt_*.h5"))
    if expected_time is None:
        if len(plots) != 2:
            raise ValueError(f"{label}: expected initial and final plots")
        initial, final = [leaf_levels(path) for path in plots]
        samples = None
    else:
        if len(plots) < 2:
            raise ValueError(f"{label}: expected at least initial and final plots")
        # Physical ordering comes from the stored HDF5 time, never filenames.
        states = [leaf_levels(path, evolving_amr=evolving_amr) for path in plots]
        times = [state["time_seconds"] for state in states]
        if not all(math.isfinite(value) for value in times):
            raise ValueError(f"{label}: nonfinite plot time")
        states.sort(key=lambda state: state["time_seconds"])
        ordered = [state["time_seconds"] for state in states]
        if any(later <= earlier for earlier, later in zip(ordered, ordered[1:])):
            raise ValueError(f"{label}: plot times are not unique and strictly increasing")
        initial, final = states[0], states[-1]
        samples = [{"time_seconds": state["time_seconds"],
                    "leaves_by_level": state["leaves_by_level"],
                    "field_min_max": state["field_min_max"]}
                   for state in states[1:-1]]
    if expect_mixed and not evolving_amr:
        if not ("0" in initial["leaves_by_level"] and
                "1" in initial["leaves_by_level"] and
                "0" in final["leaves_by_level"] and
                "1" in final["leaves_by_level"]):
            raise ValueError(f"{label}: not a coarse/fine mixed AMR run")
    elif not expect_mixed and (set(initial["leaves_by_level"]) != {"0"}
                               or set(final["leaves_by_level"]) != {"0"}):
        raise ValueError(f"{label}: expected a regular root grid")
    if expected_time is not None and not physical_times_agree(final["time_seconds"], expected_time):
        raise ValueError(f"{label}: requested physical endpoint was not reached")
    if not final["time_seconds"] > initial["time_seconds"]:
        raise ValueError(f"{label}: time did not advance")
    if final["field_min_max"]["ENUC"][1] <= 0:
        raise ValueError(f"{label}: nuclear energy rate not active")
    repairs = dict(line.split("=", 1) for line in
                   (directory / "state_repairs.txt").read_text().splitlines()
                   if "=" in line)
    if int(repairs["events"]) != 0:
        raise ValueError(f"{label}: state repairs occurred")
    logs = list(directory.glob("*_log.dat"))
    if len(logs) != 1:
        raise ValueError(f"{label}: expected one Driver log")
    log = logs[0].read_text()
    completion = re.search(r"Simulation Done\. Total Steps: (\d+)", log)
    if completion is None or (expected_steps is not None and int(completion.group(1)) != expected_steps):
        raise ValueError(f"{label}: wrong accepted step count")
    accepted_steps = int(completion.group(1))
    if accepted_steps < 1:
        raise ValueError(f"{label}: no accepted evolution steps")
    step_lines = [line for line in log.splitlines()
                  if re.match(r"^\s*\d+\s+\S+", line)]
    if len(step_lines) != accepted_steps:
        raise ValueError(f"{label}: step rows missing")
    diffusion_dt = [float(line.split()[5]) for line in step_lines]
    if not all(math.isfinite(value) and value > 0 for value in diffusion_dt):
        raise ValueError(f"{label}: invalid diffusion timestep evidence")
    with (directory / "gravity_solves.tsv").open() as stream:
        solves = list(csv.DictReader(stream, delimiter="\t"))
    if not solves:
        raise ValueError(f"{label}: no gravity solves")
    ratios = []
    for row in solves:
        residual, target = float(row["residual"]), float(row["target"])
        if not all(math.isfinite(value) for value in (residual, target)) or target <= 0:
            raise ValueError(f"{label}: invalid gravity residual")
        ratio = residual / target
        if ratio > 1:
            raise ValueError(f"{label}: gravity solve missed target")
        ratios.append(ratio)
    with next(iter(directory.glob("*_regrid.tsv"))).open() as stream:
        regrids = list(csv.DictReader(stream, delimiter="\t"))
    if expect_mixed and not any(row["topology_changed"] == "1" for row in regrids):
        raise ValueError(f"{label}: no actual AMR refinement")
    if not expect_mixed and any(row["topology_changed"] == "1" for row in regrids):
        raise ValueError(f"{label}: regular grid changed topology")
    result = {"directory": str(directory), "steps": accepted_steps,
              "requested_endpoint": expected_time,
              "initial": initial, "final": final, "state_repairs": 0,
              "gravity_solves": len(solves),
              "maximum_residual_over_target": max(ratios),
              "maximum_iterations": max(int(row["iterations"]) for row in solves),
              "minimum_diffusion_dt_seconds": min(diffusion_dt),
              "topology_changes": sum(row["topology_changed"] == "1" for row in regrids)}
    if expected_time is not None:
        result["samples"] = samples
    if evolving_amr:
        result["evolving_amr"] = True
    return result


def split_specification(specification, parameter, parser):
    """Split label:directory:parameter, reporting malformed command lines."""
    parts = specification.split(":", 2)
    if len(parts) != 3:
        parser.error(f"expected label:directory:{parameter} in {specification!r}")
    return parts


LEDGER_NAMESPACE = "observations_only"
LEDGER_SUPPORTED_NETWORKS = ("aprox13", "aprox19")
LEDGER_FIELD_UNITS = {"DENS": "g/cm^3", "ENER": "erg/cm^3", "GPOT": "cm^2/s^2"}
LEDGER_REQUIRED_METRICS = ("measure", "mass", "energy", "rhoX", "time")
LEDGER_UNCERTAINTY = (
    "longdouble summation reduces only postprocessing rounding; upstream FP64 and "
    "discretization uncertainty requires frozen reference/time-space control")
LEDGER_FIELD_SENSITIVITY_NOTE = (
    "stored-field perturbation only: each stored binary64 field is perturbed by its "
    "cell half-ULP (conservative binade spacing) against the fixed stored measures; "
    "this excludes upstream PDE/ODE, volume uncertainty, algorithm error and "
    "longdouble summation, is not scientific qualified and not a pass/fail gate, "
    "and cannot recover FP64 upstream precision")


def _project_owners():
    """Lazily import the existing project owners; no formula is reimplemented here."""
    import sys
    root = Path(__file__).resolve().parents[3]
    for path in (root / "tools", root / "validation" / "backend",
                 root / "validation" / "network"):
        if str(path) not in sys.path:
            sys.path.insert(0, str(path))
    import nse_reference
    import validate_backend_results
    import verify_microphysics_coupling
    return (validate_backend_results.read_conservation_metrics,
            verify_microphysics_coupling.nuclear_energy_delta,
            nse_reference.burn_energy_data,
            validate_backend_results.read_parameter_map)


def _text(value):
    """Decode one stored HDF5 attribute or name without inventing a default."""
    if isinstance(value, bytes):
        return value.decode("utf-8")
    if value is None:
        return ""
    return str(value)


def _digest(value, label):
    """Keep the stored sha256 text, rejecting placeholders and short records."""
    text = _text(value)
    if len(text) != 64 or any(character not in "0123456789abcdef" for character in text):
        raise ValueError(f"{label} is not a recorded sha256 digest")
    return text


def _fire_sha256(path):
    """Hash one existing source file byte-for-byte; no provenance is invented."""
    import hashlib
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def _decimal(value):
    """Serialize one longdouble observation as an explicit round-trip decimal string."""
    return np.format_float_scientific(np.longdouble(value), unique=True)


def _longdouble(value, label):
    """Restore one explicit decimal observation without an intermediate float."""
    if not isinstance(value, str):
        raise ValueError(f"{label} must be carried as an explicit decimal string")
    restored = np.longdouble(value)
    if not np.isfinite(restored):
        raise ValueError(f"{label} is not a finite observation")
    return restored


def _half_ulp(values):
    """Conservative per-cell half-spacing of stored binary64 field values.

    The spacing is read from the regular binade exponent of each value, so a
    value on a binade boundary takes the next-larger spacing.  The step is
    formed with longdouble ``ldexp`` from ``2**max(exponent - 53, -1074)``, so
    zero and minsubnormal inputs keep the minsubnormal spacing instead of
    underflowing, and ``np.spacing(maxfinite)`` is never evaluated.  This is a
    standard stored-field sensitivity, not an exact error certificate.
    """
    magnitude = np.abs(np.asarray(values, dtype=np.float64))
    _, exponent = np.frexp(magnitude)
    exponent = np.maximum(exponent.astype(np.int64) - 53, -1074)
    exponent = np.where(magnitude == 0.0, -1074, exponent)
    return np.ldexp(np.longdouble(1.0), (exponent - 1).astype(np.int64))


def _logical_keys(levels, group, label):
    """Pair each stored active leaf with its logical (level, x1, x2, x3) key."""
    stored = np.asarray(levels)
    if stored.ndim != 1 or stored.dtype.kind not in "iu":
        raise ValueError(f"{label} active-leaf levels must be a 1-D integer vector")
    columns = []
    for name in ("logical_x1", "logical_x2", "logical_x3"):
        column = np.asarray(group[name][()])
        if column.ndim != 1 or column.dtype.kind not in "iu" or column.size != stored.size:
            raise ValueError(
                f"{label} {name} must be an integer vector matching the active leaves")
        columns.append(column)
    return [(int(level), int(x1), int(x2), int(x3)) for level, x1, x2, x3 in
            zip(stored, columns[0], columns[1], columns[2])]


def _stored_bits(values):
    """Expose stored binary64 payload for the output-identity comparison."""
    return np.ascontiguousarray(values, dtype=np.float64).view(np.uint64)


def _stored_float64(dataset, label, unit=None):
    """Require the actual stored binary64 field; no dtype coercion is accepted."""
    values = dataset[()]
    if values.dtype != np.float64:
        raise ValueError(
            f"{label} must be stored as binary64/f8, not {values.dtype}")
    if unit is not None and _text(dataset.attrs.get("unit")) != unit:
        raise ValueError(f"{label} stored unit is not {unit!r}")
    return values


def _periodic_faces(boundary_identity, dim):
    """Require the frozen periodic branch so zero external flux is interpretable."""
    faces = [face for face in _text(boundary_identity).split("faces=")[-1].split(";") if face]
    if len(faces) != 2 * dim or any(face != "periodic" for face in faces):
        raise ValueError(
            "unsupported boundary identity for the observational ledger: only the "
            "periodic Cartesian branch can be read as zero external flux")
    return faces


def read_ledger_sample(validator, parameters, checkpoint, plot):
    """Read one existing plot/checkpoint pair as an observational ledger sample.

    The stored NativeGrid cell measures integrate the actual checkpoint state
    with the existing C++ metrics.  No coordinate or volume formula is
    recomputed here, and no missing potential or geometry is defaulted.  The
    integrated observations are carried as explicit longdouble decimal strings
    so that the endpoint subtraction keeps the postprocessing precision; the
    result is observation-only bookkeeping, not a scientific certificate.
    """
    read_conservation_metrics, _nuclear, burn_energy_data, read_parameter_map = _project_owners()
    validator, parameters = Path(validator), Path(parameters)
    checkpoint, plot = Path(checkpoint), Path(plot)
    parameters_before = _fire_sha256(parameters)
    metrics = read_conservation_metrics(validator, checkpoint, parameters)
    if _fire_sha256(parameters) != parameters_before:
        raise ValueError("parameter file changed while reading the ledger sample")
    if _text(metrics.get("measure")) != "physical_cell_volume":
        raise ValueError("ledger sample requires parameter-bound physical cell volumes")
    if _digest(metrics.get("parameter_sha256"), "checkpoint metric parameter") != parameters_before:
        raise ValueError("checkpoint metrics were computed from a different parameter file")
    for key in LEDGER_REQUIRED_METRICS:
        if key not in metrics:
            raise ValueError(f"checkpoint metrics omit the required key {key!r}; no zero default")
    for key in ("mass", "energy", "time"):
        value = metrics[key]
        if isinstance(value, bool) or not isinstance(value, (int, float)) \
                or not math.isfinite(float(value)):
            raise ValueError(f"checkpoint metric {key!r} is not a finite recorded number")
    metric_rhoX = metrics["rhoX"]
    if not isinstance(metric_rhoX, (list, tuple)) or not metric_rhoX \
            or not all(isinstance(value, (int, float)) and not isinstance(value, bool)
                       and math.isfinite(float(value)) for value in metric_rhoX):
        raise ValueError("checkpoint metric rhoX must be a finite species vector")
    parameter_map = read_parameter_map(parameters)
    if _fire_sha256(parameters) != parameters_before:
        raise ValueError("parameter file changed while reading the ledger sample")

    with h5py.File(plot, "r") as handle:
        if _text(handle.attrs.get("plot_publication_version")) != "arch-plot-publication-1" \
                or _text(handle.attrs.get("plot_publication_state")) != "complete" \
                or _text(handle.attrs.get("plot_identity_state")) != "recorded":
            raise ValueError("plot is not a complete formal publication record")
        plot_time = float(handle.attrs["time"])
        if not math.isfinite(plot_time):
            raise ValueError("plot physical time is not finite")
        plot_dim = int(handle.attrs["dim"])
        plot_geometry = _text(handle.attrs.get("geometry"))
        identity = handle["SourceIdentity"]
        raw_config_sha256 = _digest(identity.attrs.get("raw_config_sha256"),
                                    "plot raw config")
        plot_eos_type = _text(identity.attrs.get("eos_type"))
        plot_eos_table = _digest(identity.attrs.get("eos_table_sha256"),
                                 "plot EOS table")
        binary_sha256 = _digest(identity.attrs.get("binary_sha256"), "plot binary")
        if "species_names" not in identity:
            raise ValueError("plot SourceIdentity carries no species_names record")
        plot_species = [_text(name) for name in identity["species_names"][()]]
        native = handle["NativeGrid"]
        if _text(native.attrs.get("measure_source")) != "GridMetrics::CellVolume":
            raise ValueError("plot native measure_source is not GridMetrics::CellVolume")
        measure_unit = _text(native.attrs.get("measure_unit"))
        measure_normalization = _text(native.attrs.get("measure_normalization"))
        native_geometry = _text(native.attrs.get("native_geometry"))
        for name, unit in LEDGER_FIELD_UNITS.items():
            if f"Data/{name}" not in handle:
                raise ValueError(f"plot is missing Data/{name}; no zero potential fallback")
        dens = _stored_float64(handle["Data/DENS"], "plot Data/DENS",
                               LEDGER_FIELD_UNITS["DENS"])
        ener = _stored_float64(handle["Data/ENER"], "plot Data/ENER",
                               LEDGER_FIELD_UNITS["ENER"])
        gpot = _stored_float64(handle["Data/GPOT"], "plot Data/GPOT",
                               LEDGER_FIELD_UNITS["GPOT"])
        measure = _stored_float64(native["cell_measure"], "plot cell_measure")
        plot_keys = _logical_keys(handle["Grid/level"][()], native, "plot NativeGrid")
        blocks = len(plot_keys)
        if blocks < 1 or dens.ndim < 1 or dens.shape[0] != blocks:
            raise ValueError("plot active leaf count disagrees with Data/DENS")
        per_block = int(np.prod(dens.shape[1:])) if dens.ndim > 1 else 1
        if per_block < 1 or ener.shape != dens.shape or gpot.shape != dens.shape:
            raise ValueError("plot Data field layouts disagree with Data/DENS")
        if measure.ndim != 1 or measure.size != blocks * per_block:
            raise ValueError("plot stored cell_measure does not match the active Data layout")
        dens = dens.reshape(blocks, per_block)
        ener = ener.reshape(blocks, per_block)
        gpot = gpot.reshape(blocks, per_block)
        measure = measure.reshape(blocks, per_block)

    with h5py.File(checkpoint, "r") as handle:
        checkpoint_time = float(handle.attrs["time"])
        if not math.isfinite(checkpoint_time):
            raise ValueError("checkpoint physical time is not finite")
        step = int(handle.attrs["step"])
        checkpoint_dim = int(handle.attrs["dim"])
        checkpoint_geometry = _text(handle.attrs.get("geometry"))
        num_species = int(handle.attrs["num_species"])
        cells_per_block = int(handle.attrs["cells_per_block"])
        network = _text(handle.attrs.get("active_network"))
        checkpoint_eos_type = _text(handle.attrs.get("eos_type"))
        checkpoint_eos_table = _text(handle.attrs.get("eos_table_sha256"))
        boundary_identity = _text(handle.attrs.get("boundary_identity"))
        gravity_type = _text(handle.attrs.get("gravity_type"))
        gravity_boundary = _text(handle.attrs.get("gravity_boundary"))
        burn_enabled = int(handle.attrs["burn_enabled"])
        blocks_group = handle["Blocks"]
        checkpoint_keys = _logical_keys(blocks_group["level"][()], blocks_group,
                                        "checkpoint Blocks")
        rho = _stored_float64(handle["Data/rho"], "checkpoint Data/rho")
        eng = _stored_float64(handle["Data/eng"], "checkpoint Data/eng")
        rhoX = _stored_float64(handle["Data/rhoX"], "checkpoint Data/rhoX")
        momenta = []
        for name in ("mom_u", "mom_v", "mom_w"):
            if f"Data/{name}" not in handle:
                raise ValueError(
                    f"checkpoint is missing Data/{name}; no zero momentum fallback")
            momenta.append(_stored_float64(handle[f"Data/{name}"],
                                           f"checkpoint Data/{name}"))
        if "Species/name" not in handle:
            raise ValueError("checkpoint carries no Species/name record")
        names = [_text(name) for name in handle["Species/name"][()]]
        species_A = np.asarray(handle["Species/A"][()], dtype=np.float64)
        species_Z = np.asarray(handle["Species/Z"][()], dtype=np.float64)

    if plot_time != checkpoint_time:
        raise ValueError("plot and checkpoint physical times differ")
    if plot_dim != checkpoint_dim or checkpoint_dim not in (2, 3):
        raise ValueError("unsupported ledger dimension: the frozen branch is Cartesian 2D/3D")
    if plot_geometry != "cartesian" or checkpoint_geometry != "cartesian" \
            or native_geometry != "cartesian":
        raise ValueError("unsupported ledger geometry: the frozen branch is Cartesian only")
    if network not in LEDGER_SUPPORTED_NETWORKS:
        raise ValueError(
            f"unsupported ledger network {network!r}: only zero-flux aprox13/aprox19 is read")
    if gravity_type != "self" or gravity_boundary != "periodic":
        raise ValueError("unsupported gravity scope: the frozen branch is self-gravity, periodic")
    if not burn_enabled:
        raise ValueError("unsupported ledger scope: the frozen branch requires an active burn network")
    expected_unit = "cm^2" if checkpoint_dim == 2 else "cm^3"
    expected_normalization = ("per_unit_transverse_length" if checkpoint_dim == 2
                              else "full_volume")
    if measure_unit != expected_unit or measure_normalization != expected_normalization:
        raise ValueError("plot stored measure unit/normalization disagrees with the frozen branch")
    if raw_config_sha256 != parameters_before:
        raise ValueError("plot raw config identity does not match the parameter file")
    if plot_eos_type != checkpoint_eos_type:
        raise ValueError("plot and checkpoint EOS type identities disagree")
    if plot_eos_table != _digest(checkpoint_eos_table, "checkpoint EOS table"):
        raise ValueError("plot and checkpoint EOS table identities disagree")
    if "geometry" in metrics and _text(metrics["geometry"]) != checkpoint_geometry:
        raise ValueError("checkpoint metrics geometry disagrees with the checkpoint")
    if not physical_times_agree(float(metrics["time"]), checkpoint_time):
        raise ValueError("checkpoint metrics physical time disagrees with the checkpoint")

    def parameter(key):
        if key not in parameter_map:
            raise ValueError(
                f"parameter file is missing {key!r} for the frozen ledger branch")
        return parameter_map[key]

    if parameter("geometry") != "cartesian":
        raise ValueError("parameter geometry is not the frozen Cartesian branch")
    if parameter("gravity_type") != "self" or parameter("gravity_boundary") != "periodic":
        raise ValueError("parameter gravity scope is not the frozen self/periodic branch")
    if parameter("use_burn") != "true":
        raise ValueError("parameter burn switch is not the frozen active network branch")
    if parameter("network_name") != network:
        raise ValueError("parameter network disagrees with the checkpoint active network")
    if parameter("eos_type") != checkpoint_eos_type:
        raise ValueError("parameter EOS family disagrees with the checkpoint EOS type")
    identity_faces = _periodic_faces(boundary_identity, checkpoint_dim)
    parameter_faces = [parameter(f"x{axis}{side}_boundary_type")
                       for axis in range(1, checkpoint_dim + 1) for side in ("l", "r")]
    if parameter_faces != identity_faces:
        raise ValueError(
            "parameter active face identity disagrees with the checkpoint boundary identity")

    if num_species < 1 or len(names) != num_species or species_A.size != num_species \
            or species_Z.size != num_species:
        raise ValueError("checkpoint species layout is inconsistent with num_species")
    if len(metric_rhoX) != num_species:
        raise ValueError("checkpoint metric rhoX length disagrees with num_species")
    if list(names) != plot_species:
        raise ValueError("checkpoint species names/order disagree with SourceIdentity/species_names")
    if cells_per_block < 1 or rho.shape != (len(checkpoint_keys), cells_per_block) \
            or eng.shape != rho.shape:
        raise ValueError("checkpoint Data/rho and Data/eng must match (blocks, cells_per_block)")
    if rhoX.shape != (num_species, len(checkpoint_keys), cells_per_block):
        raise ValueError(
            "checkpoint Data/rhoX must be exactly (num_species, blocks, cells_per_block)")
    for name, values in zip(("mom_u", "mom_v", "mom_w"), momenta):
        if values.shape != rho.shape:
            raise ValueError(
                f"checkpoint Data/{name} must exactly match the rho shape; "
                "no zero momentum fallback")
    if per_block != cells_per_block:
        raise ValueError("plot cells per active leaf disagree with the checkpoint cells_per_block")
    if len(set(plot_keys)) != len(plot_keys) or len(set(checkpoint_keys)) != len(checkpoint_keys):
        raise ValueError("duplicate logical leaf keys in the pair")
    if set(plot_keys) != set(checkpoint_keys):
        raise ValueError("plot and checkpoint logical leaf key sets differ")

    nuclear = burn_energy_data(network)
    reference_A = np.asarray(nuclear["arrays"]["AION"], dtype=np.float64)
    reference_Z = np.asarray(nuclear["arrays"]["ZION"], dtype=np.float64)
    if not np.array_equal(species_A, reference_A) or not np.array_equal(species_Z, reference_Z):
        raise ValueError("checkpoint species A/Z do not match the actual network reference")
    nuclear_data_sha256 = _digest(nuclear.get("data_sha256"), "nuclear data identity")
    nuclear_conversion_sha256 = _digest(nuclear.get("conversion_sha256"),
                                        "nuclear conversion identity")
    nuclear_basis = _text(nuclear.get("burn_energy_basis"))
    if not nuclear_basis:
        raise ValueError("network burn-energy basis is not recorded")
    if not np.all(np.isfinite(rho)) or not np.all(rho > 0):
        raise ValueError("checkpoint rho must be finite and positive")
    if not np.all(np.isfinite(eng)):
        raise ValueError("checkpoint eng must be finite")
    if not np.all(np.isfinite(rhoX)):
        raise ValueError("checkpoint rhoX must be finite")
    for name, values in zip(("mom_u", "mom_v", "mom_w"), momenta):
        if not np.all(np.isfinite(values)):
            raise ValueError(f"checkpoint Data/{name} must be finite")
    if not np.all(np.isfinite(dens)) or not np.all(dens > 0):
        raise ValueError("plot DENS must be finite and positive")
    if not np.all(np.isfinite(ener)):
        raise ValueError("plot ENER must be finite")
    if not np.all(np.isfinite(gpot)):
        raise ValueError("plot GPOT must be finite; no zero potential fallback")
    if not np.all(np.isfinite(measure)) or not np.all(measure > 0):
        raise ValueError("stored cell_measure must be finite and positive")

    # Frozen Cartesian physical internal energy from the stored momenta; the
    # existing positive-rho rule and any EOS inversion are left untouched.
    density_state = rho.reshape(-1).astype(np.longdouble)
    kinetic_state = sum(momenta[index].reshape(-1).astype(np.longdouble) ** 2
                        for index in range(3))
    specific_internal_energy = (eng.reshape(-1).astype(np.longdouble) / density_state
                                - kinetic_state / (2 * density_state * density_state))
    if not np.all(np.isfinite(specific_internal_energy)):
        raise ValueError("checkpoint Cartesian specific internal energy is not finite")
    if not np.all(specific_internal_energy > 0):
        raise ValueError("checkpoint Cartesian specific internal energy is not positive")
    species_state = rhoX.reshape(num_species, -1).astype(np.longdouble)
    physical_state = {
        "min_specific_internal_energy": _decimal(np.min(specific_internal_energy)),
        "min_species_density": _decimal(np.min(species_state)),
        "max_abs_species_sum_minus_rho_relative": _decimal(np.max(
            np.abs(np.sum(species_state, axis=0) - density_state) / density_state))}

    order = {key: index for index, key in enumerate(plot_keys)}
    permutation = [order[key] for key in checkpoint_keys]
    if not np.array_equal(_stored_bits(dens[permutation]), _stored_bits(rho)):
        raise ValueError("plot DENS does not match checkpoint rho bit-exactly")
    if not np.array_equal(_stored_bits(ener[permutation]), _stored_bits(eng)):
        raise ValueError("plot ENER does not match checkpoint eng bit-exactly")

    volume = measure[permutation].reshape(-1).astype(np.longdouble)
    density = rho.reshape(-1).astype(np.longdouble)
    energy = eng.reshape(-1).astype(np.longdouble)
    potential = gpot[permutation].reshape(-1).astype(np.longdouble)
    species = rhoX.reshape(num_species, -1).astype(np.longdouble)
    total_volume = np.sum(volume, dtype=np.longdouble)
    if not np.isfinite(total_volume) or total_volume <= 0:
        raise ValueError("stored cell measures do not define a positive physical volume")
    mass = np.sum(volume * density, dtype=np.longdouble)
    egas = np.sum(volume * energy, dtype=np.longdouble)
    species_integrals = [np.sum(volume * species[index], dtype=np.longdouble)
                         for index in range(num_species)]
    w = np.longdouble(0.5) * np.sum(volume * density * potential, dtype=np.longdouble)
    mean_phi = np.sum(volume * potential, dtype=np.longdouble) / total_volume
    if not all(np.isfinite(value) for value in (mass, egas, w, mean_phi)) \
            or not all(np.isfinite(value) for value in species_integrals):
        raise ValueError("integrated ledger observations are not finite")
    stored_energy = eng.reshape(-1)
    stored_species = rhoX.reshape(num_species, -1)
    egas_sensitivity = np.sum(volume * _half_ulp(stored_energy), dtype=np.longdouble)
    species_sensitivity = [np.sum(volume * _half_ulp(stored_species[index]),
                                  dtype=np.longdouble)
                           for index in range(num_species)]
    if not np.isfinite(egas_sensitivity) \
            or not all(np.isfinite(value) for value in species_sensitivity):
        raise ValueError("integrated stored-field sensitivities are not finite")
    metric_report = {"measure": _text(metrics["measure"]),
                     "mass": float(metrics["mass"]),
                     "energy": float(metrics["energy"]),
                     "rhoX": [float(value) for value in metric_rhoX],
                     "time": float(metrics["time"])}
    for optional in ("geometry", "step", "blocks"):
        if optional in metrics:
            metric_report[optional] = metrics[optional]
    return {"checkpoint": str(checkpoint), "plot": str(plot),
            "checkpoint_sha256": _fire_sha256(checkpoint),
            "plot_sha256": _fire_sha256(plot),
            "parameters_sha256": parameters_before,
            "binary_sha256": binary_sha256,
            "eos_type": checkpoint_eos_type,
            "eos_table_sha256": plot_eos_table,
            "network": network,
            "nuclear_data_sha256": nuclear_data_sha256,
            "nuclear_conversion_sha256": nuclear_conversion_sha256,
            "nuclear_basis": nuclear_basis,
            "species_names": list(names),
            "species_A": [float(value) for value in species_A],
            "species_Z": [float(value) for value in species_Z],
            "time_seconds": plot_time, "step": step,
            "dim": checkpoint_dim, "geometry": checkpoint_geometry,
            "boundary_identity": boundary_identity, "gauge": gravity_boundary,
            "measure_source": "GridMetrics::CellVolume",
            "measure_unit": measure_unit, "measure_normalization": measure_normalization,
            "blocks": len(checkpoint_keys), "cells_per_active_leaf": per_block,
            "mass": _decimal(mass), "egas": _decimal(egas), "w": _decimal(w),
            "mean_phi": _decimal(mean_phi),
            "species_integrals": [_decimal(value) for value in species_integrals],
            "physical_state": physical_state,
            "egas_sensitivity": _decimal(egas_sensitivity),
            "species_integral_sensitivity": [_decimal(value) for value in species_sensitivity],
            "sensitivity_note": LEDGER_FIELD_SENSITIVITY_NOTE,
            "checkpoint_metrics": metric_report}


def endpoint_ledger(before, after, data):
    """Pair two ledger samples with the existing endpoint nuclear-energy law.

    Only observations are returned.  The residual R = dEgas + dW - Q is reported
    with its own scales; no acceptance threshold and no upstream precision
    claim is introduced here.
    """
    _metrics, nuclear_energy_delta, _burn, _read_parameters = _project_owners()
    for key in ("parameters_sha256", "network", "nuclear_data_sha256",
                "nuclear_conversion_sha256", "nuclear_basis", "measure_source",
                "measure_unit", "measure_normalization", "binary_sha256",
                "eos_type", "eos_table_sha256", "gauge", "dim", "geometry",
                "boundary_identity", "species_names", "species_A", "species_Z"):
        if before[key] != after[key]:
            raise ValueError(f"ledger samples disagree on {key}")
    if not after["time_seconds"] > before["time_seconds"]:
        raise ValueError("ledger samples must be strictly increasing in physical time")
    for key in ("egas_sensitivity", "species_integral_sensitivity"):
        if key not in before or key not in after:
            raise ValueError(
                f"ledger samples must carry the mandatory {key!r} observation; "
                "no zero fallback")
    if _digest(data.get("data_sha256"), "supplied nuclear data") \
            != before["nuclear_data_sha256"]:
        raise ValueError("supplied nuclear data identity disagrees with the ledger samples")
    if _digest(data.get("conversion_sha256"), "supplied nuclear conversion") \
            != before["nuclear_conversion_sha256"]:
        raise ValueError("supplied nuclear conversion identity disagrees with the ledger samples")
    if _text(data.get("burn_energy_basis")) != before["nuclear_basis"]:
        raise ValueError("supplied nuclear energy basis disagrees with the ledger samples")
    if not np.array_equal(np.asarray(data["arrays"]["AION"], dtype=np.float64),
                          np.asarray(before["species_A"], dtype=np.float64)) \
            or not np.array_equal(np.asarray(data["arrays"]["ZION"], dtype=np.float64),
                                  np.asarray(before["species_Z"], dtype=np.float64)):
        raise ValueError("supplied nuclear A/Z identity disagrees with the ledger samples")
    before_species = [_longdouble(value, "before species integral")
                      for value in before["species_integrals"]]
    after_species = [_longdouble(value, "after species integral")
                     for value in after["species_integrals"]]
    if len(before_species) != len(after_species) or not before_species:
        raise ValueError("ledger samples disagree on the species integral vector")
    delta = (np.asarray(after_species, dtype=np.longdouble)
             - np.asarray(before_species, dtype=np.longdouble))
    q = np.longdouble(nuclear_energy_delta(data, delta))
    aion = np.asarray(data["arrays"]["AION"], dtype=np.longdouble)
    zion = np.asarray(data["arrays"]["ZION"], dtype=np.longdouble)
    charge = np.sum(delta * (zion / aion), dtype=np.longdouble)
    delta_mass = _longdouble(after["mass"], "after mass") - _longdouble(before["mass"],
                                                                       "before mass")
    delta_egas = _longdouble(after["egas"], "after egas") - _longdouble(before["egas"],
                                                                       "before egas")
    w_before = _longdouble(before["w"], "before W")
    w_after = _longdouble(after["w"], "after W")
    if not all(np.isfinite(value) for value in (q, charge, delta_mass, delta_egas, w_before, w_after)):
        raise ValueError("endpoint ledger observations are not finite")
    delta_w = w_after - w_before
    residual = delta_egas + delta_w - q
    absolute_term_sums = {"delta_egas": abs(delta_egas), "delta_w": abs(delta_w),
                          "q": abs(q), "total": abs(delta_egas) + abs(delta_w) + abs(q)}
    egas_sensitivity = (_longdouble(before["egas_sensitivity"], "before egas sensitivity")
                        + _longdouble(after["egas_sensitivity"], "after egas sensitivity"))
    before_sensitivity = [_longdouble(value, "before species sensitivity")
                          for value in before["species_integral_sensitivity"]]
    after_sensitivity = [_longdouble(value, "after species sensitivity")
                         for value in after["species_integral_sensitivity"]]
    if len(before_sensitivity) != len(before_species) \
            or len(after_sensitivity) != len(before_species):
        raise ValueError("ledger samples disagree on the species sensitivity vector")
    species_sensitivity = (np.asarray(before_sensitivity, dtype=np.longdouble)
                           + np.asarray(after_sensitivity, dtype=np.longdouble))
    # The stored-field Q sensitivity reuses the frozen nuclear law on a
    # one-species uncertainty basis; no coefficient or constant is copied.
    q_sensitivity = np.sum(np.abs(nuclear_energy_delta(
        data, np.diag(species_sensitivity))), dtype=np.longdouble)
    if not np.isfinite(egas_sensitivity) or not np.isfinite(q_sensitivity):
        raise ValueError("endpoint stored-field sensitivities are not finite")
    egas_scale = max(abs(_longdouble(before["egas"], "before egas")),
                     abs(_longdouble(after["egas"], "after egas")))
    q_over_egas = None if egas_scale == 0 else _decimal(q / egas_scale)
    if q_sensitivity > 0:
        q_over_sensitivity = _decimal(abs(q) / q_sensitivity)
        ratio_state = "reported"
    else:
        q_over_sensitivity = None
        ratio_state = "zero_stored_field_sensitivity"
    return {"namespace": LEDGER_NAMESPACE, "scientific_qualified": False,
            "q": float(q), "delta_mass": float(delta_mass),
            "delta_charge": float(charge), "delta_egas": float(delta_egas),
            "w_before": float(w_before), "w_after": float(w_after),
            "delta_w": float(delta_w), "residual": float(residual),
            "absolute_terms": {name: float(value)
                               for name, value in absolute_term_sums.items()},
            "scales": {"energy": float(max(abs(np.longdouble(_longdouble(before["egas"],
                                                                         "before egas"))),
                                           abs(np.longdouble(_longdouble(after["egas"],
                                                                         "after egas"))))),
                       "q": float(abs(q)),
                       "w": float(max(abs(w_before), abs(w_after))),
                       "mass": float(max(abs(np.longdouble(_longdouble(before["mass"],
                                                                       "before mass"))),
                                         abs(np.longdouble(_longdouble(after["mass"],
                                                                       "after mass")))))},
            "species_mass_change": [float(value) for value in delta],
            "q_over_egas": q_over_egas,
            "q_over_sensitivity": q_over_sensitivity,
            "sensitivity": {"delta_egas": _decimal(egas_sensitivity),
                            "q": _decimal(q_sensitivity),
                            "ratio_state": ratio_state,
                            "basis": "stored_field_half_ulp",
                            "note": LEDGER_FIELD_SENSITIVITY_NOTE},
            "roundoff": {
                "input_machine_eps": float(np.finfo(np.float64).eps),
                "longdouble_machine_eps": float(np.finfo(np.longdouble).eps),
                "longdouble_precision_bits": int(np.finfo(np.longdouble).nmant) + 1,
                "absolute_term_sums": {name: float(value)
                                       for name, value in absolute_term_sums.items()},
                "note": ("roundoff diagnostics only; no strict certificate and no "
                         "recovered upstream precision is claimed")},
            "uncertainty": LEDGER_UNCERTAINTY}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--run", action="append", default=[],
                        help="label:output-directory:expected-steps")
    parser.add_argument("--endpoint-run", action="append", default=[],
                        help="label:output-directory:physical-time-seconds")
    parser.add_argument("--evolving-amr", action="store_true",
                        help="endpoint runs only: allow changing AMR level distributions; "
                             "still require an actual topology change")
    parser.add_argument("--parameters", type=Path,
                        help="existing parameters input for the observational ledger")
    parser.add_argument("--checkpoint-validator", type=Path,
                        help="existing checkpoint metrics validator")
    parser.add_argument("--ledger-pair", nargs=2, action="append", default=[],
                        metavar=("CHECKPOINT", "PLOT"))
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    if not args.run and not args.endpoint_run:
        parser.error("at least one --run or --endpoint-run is required")
    if args.evolving_amr and (args.run or not args.endpoint_run):
        parser.error("--evolving-amr requires only --endpoint-run lanes")
    jobs = []
    labels = set()
    for specification in args.run:
        label, path, steps = split_specification(specification, "expected-steps", parser)
        if label in labels:
            parser.error(f"duplicate run label {label!r}")
        labels.add(label)
        try:
            jobs.append((label, Path(path), int(steps), None))
        except ValueError:
            parser.error(f"expected an integer step count in {specification!r}")
    for specification in args.endpoint_run:
        label, path, endpoint = split_specification(specification,
                                                    "physical-time-seconds", parser)
        if label in labels:
            parser.error(f"duplicate run label {label!r}")
        labels.add(label)
        try:
            jobs.append((label, Path(path), None, float(endpoint)))
        except ValueError:
            parser.error(f"expected a numeric physical time in {specification!r}")
    ledger_requested = bool(args.parameters or args.checkpoint_validator or args.ledger_pair)
    ledger_pairs = []
    if ledger_requested:
        if args.parameters is None or args.checkpoint_validator is None or not args.ledger_pair:
            parser.error("--parameters, --checkpoint-validator and --ledger-pair are required "
                         "together")
        if len(args.ledger_pair) < 2:
            parser.error("the ledger branch requires at least two --ledger-pair CHECKPOINT PLOT "
                         "arguments")
        if len(jobs) != 1:
            parser.error("the ledger branch requires exactly one --run or --endpoint-run lane")
        ledger_pairs = [(Path(pair[0]), Path(pair[1])) for pair in args.ledger_pair]
    results = {}
    for label, path, steps, endpoint in jobs:
        results[label] = verify(label, path, steps, expected_time=endpoint,
                                evolving_amr=args.evolving_amr)
    if ledger_pairs:
        label = next(iter(results))
        lane = results[label]
        lane_plots = {plot.resolve() for plot in
                      Path(lane["directory"]).glob("*_plt_*.h5")}
        for _checkpoint, plot in ledger_pairs:
            if plot.resolve() not in lane_plots:
                raise ValueError(
                    f"{label}: ledger plot {plot} was not audited in run directory "
                    f"{lane['directory']}")
        _metrics_owner, _nuclear, burn_energy_data, _parameters_owner = _project_owners()
        samples = [read_ledger_sample(args.checkpoint_validator, args.parameters,
                                      checkpoint, plot)
                   for checkpoint, plot in ledger_pairs]
        samples.sort(key=lambda sample: sample["time_seconds"])
        times = [sample["time_seconds"] for sample in samples]
        if any(later <= earlier for earlier, later in zip(times, times[1:])):
            parser.error("--ledger-pair times are not unique and strictly increasing")
        if not physical_times_agree(times[0], lane["initial"]["time_seconds"]):
            raise ValueError(f"{label}: ledger pairs do not begin at the run initial time")
        if not physical_times_agree(times[-1], lane["final"]["time_seconds"]):
            raise ValueError(f"{label}: ledger pairs do not end at the run final time")
        if lane["requested_endpoint"] is not None \
                and not physical_times_agree(times[-1], lane["requested_endpoint"]):
            raise ValueError(f"{label}: ledger pairs do not reach the requested physical endpoint")
        lane["scientific_ledger"] = {
            "namespace": LEDGER_NAMESPACE, "scientific_qualified": False,
            "samples": samples,
            "endpoint": endpoint_ledger(samples[0], samples[-1],
                                        burn_energy_data(samples[0]["network"]))}
    report = json.dumps(results, indent=2, sort_keys=True) + "\n"
    if args.output:
        args.output.write_text(report)
    else:
        print(report, end="")


if __name__ == "__main__":
    main()
