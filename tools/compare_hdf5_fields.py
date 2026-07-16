#!/usr/bin/env python3
"""Compare all common numeric fields in two ARCH HDF5 plot files.

The comparison is symmetric: relative norm denominators use the larger norm of
the two fields, with a configurable scale floor.  By default the command reports
differences without imposing a physics tolerance.  Supplying --rtol and/or
--atol enables an explicit pass/fail gate based on each field's maximum error.

Exit status:
  0  structurally valid comparison and all requested tolerances pass
  1  a requested numeric tolerance fails
  2  missing fields, shape mismatch, non-finite data, or unreadable input
"""

from __future__ import annotations

import argparse
import json
import math
import sys
from pathlib import Path
from typing import Any, Dict, Iterable, Mapping, Optional, Sequence, Tuple

try:
    import h5py
    import numpy as np
except ImportError as exc:  # pragma: no cover - exercised only on incomplete hosts
    raise SystemExit(f"compare_hdf5_fields.py requires h5py and numpy: {exc}")


def parse_args(argv: Optional[Sequence[str]] = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Compare every common numeric dataset below /Data in two ARCH HDF5 "
            "plot files. No evolved-flow tolerance is assumed by default."
        )
    )
    parser.add_argument("candidate", type=Path, help="candidate CPU/CUDA ARCH HDF5 file")
    parser.add_argument("reference", type=Path, help="reference ARCH HDF5 file")
    parser.add_argument(
        "--rtol",
        type=float,
        default=None,
        help="optional relative Linf tolerance; no default tolerance is imposed",
    )
    parser.add_argument(
        "--atol",
        type=float,
        default=None,
        help="optional absolute Linf tolerance; no default tolerance is imposed",
    )
    parser.add_argument(
        "--scale-floor",
        type=float,
        default=1.0e-30,
        help="positive floor for relative-norm denominators (default: %(default)g)",
    )
    parser.add_argument(
        "--species-prefix",
        default="X_",
        help="dataset basename prefix used to identify mass fractions (default: %(default)s)",
    )
    parser.add_argument(
        "--allow-missing",
        action="store_true",
        help="report datasets present in only one file without treating them as structural failure",
    )
    parser.add_argument(
        "--json",
        dest="json_output",
        metavar="PATH",
        help="write the complete machine-readable report to PATH, or '-' for stdout",
    )
    args = parser.parse_args(argv)

    for name in ("rtol", "atol"):
        value = getattr(args, name)
        if value is not None and (not math.isfinite(value) or value < 0.0):
            parser.error(f"--{name} must be finite and non-negative")
    if not math.isfinite(args.scale_floor) or args.scale_floor <= 0.0:
        parser.error("--scale-floor must be finite and positive")
    if not args.species_prefix:
        parser.error("--species-prefix must not be empty")
    return args


def json_value(value: Any) -> Any:
    """Convert HDF5/NumPy attribute values into JSON-compatible objects."""

    if isinstance(value, bytes):
        return value.decode("utf-8", errors="replace")
    if isinstance(value, np.generic):
        return json_value(value.item())
    if isinstance(value, np.ndarray):
        return [json_value(item) for item in value.tolist()]
    if isinstance(value, (list, tuple)):
        return [json_value(item) for item in value]
    if isinstance(value, Mapping):
        return {str(key): json_value(item) for key, item in value.items()}
    if isinstance(value, (str, int, float, bool)) or value is None:
        return value
    return str(value)


def numeric_datasets(group: h5py.Group) -> Dict[str, h5py.Dataset]:
    """Return every numeric dataset below *group*, keyed by relative path."""

    datasets: Dict[str, h5py.Dataset] = {}

    def visitor(name: str, obj: h5py.Dataset) -> None:
        if isinstance(obj, h5py.Dataset) and np.issubdtype(obj.dtype, np.number):
            datasets[name] = obj

    group.visititems(visitor)
    return datasets


def symmetric_norm_metrics(candidate: np.ndarray, reference: np.ndarray, floor: float) -> Dict[str, float]:
    """Compute symmetric relative L1/L2/Linf metrics with robust denominators."""

    delta = candidate - reference
    abs_delta = np.abs(delta)
    n_values = max(int(delta.size), 1)

    l1_delta = float(np.sum(abs_delta, dtype=np.float64))
    l2_delta = float(np.linalg.norm(delta.ravel()))
    linf_delta = float(np.max(abs_delta)) if delta.size else 0.0

    l1_candidate = float(np.sum(np.abs(candidate), dtype=np.float64))
    l1_reference = float(np.sum(np.abs(reference), dtype=np.float64))
    l2_candidate = float(np.linalg.norm(candidate.ravel()))
    l2_reference = float(np.linalg.norm(reference.ravel()))
    linf_candidate = float(np.max(np.abs(candidate))) if candidate.size else 0.0
    linf_reference = float(np.max(np.abs(reference))) if reference.size else 0.0

    scale_l1 = max(l1_candidate, l1_reference, floor * n_values)
    scale_l2 = max(l2_candidate, l2_reference, floor * math.sqrt(n_values))
    scale_linf = max(linf_candidate, linf_reference, floor)

    return {
        "max_abs": linf_delta,
        "rel_l1": l1_delta / scale_l1,
        "rel_l2": l2_delta / scale_l2,
        "rel_linf": linf_delta / scale_linf,
        "scale_l1": scale_l1,
        "scale_l2": scale_l2,
        "scale_linf": scale_linf,
    }


def tolerance_pass(metrics: Mapping[str, float], rtol: Optional[float], atol: Optional[float]) -> Optional[bool]:
    if rtol is None and atol is None:
        return None
    relative = 0.0 if rtol is None else rtol
    absolute = 0.0 if atol is None else atol
    limit = absolute + relative * metrics["scale_linf"]
    return metrics["max_abs"] <= limit


def sum_species(
    datasets: Mapping[str, h5py.Dataset], species_names: Iterable[str]
) -> Tuple[Optional[np.ndarray], Optional[str]]:
    total: Optional[np.ndarray] = None
    expected_shape: Optional[Tuple[int, ...]] = None
    for name in species_names:
        dataset = datasets[name]
        shape = tuple(dataset.shape)
        if expected_shape is None:
            expected_shape = shape
            total = np.zeros(shape, dtype=np.float64)
        elif shape != expected_shape:
            return None, f"species dataset {name!r} has shape {shape}, expected {expected_shape}"
        assert total is not None
        total += np.asarray(dataset[...], dtype=np.float64)
    return total, None


def species_sum_report(
    candidate_data: Mapping[str, h5py.Dataset],
    reference_data: Mapping[str, h5py.Dataset],
    prefix: str,
    floor: float,
) -> Dict[str, Any]:
    candidate_names = sorted(
        name for name in candidate_data if name.rsplit("/", 1)[-1].startswith(prefix)
    )
    reference_names = sorted(
        name for name in reference_data if name.rsplit("/", 1)[-1].startswith(prefix)
    )
    report: Dict[str, Any] = {
        "prefix": prefix,
        "candidate_fields": candidate_names,
        "reference_fields": reference_names,
        "status": "skipped",
    }
    if not candidate_names and not reference_names:
        report["reason"] = "no species datasets found"
        return report

    candidate_sum, candidate_error = sum_species(candidate_data, candidate_names)
    reference_sum, reference_error = sum_species(reference_data, reference_names)
    if candidate_error or reference_error:
        report.update(
            status="error",
            errors=[message for message in (candidate_error, reference_error) if message],
        )
        return report
    assert candidate_sum is not None and reference_sum is not None

    candidate_finite = np.isfinite(candidate_sum)
    reference_finite = np.isfinite(reference_sum)
    report["candidate_nonfinite"] = int(candidate_sum.size - np.count_nonzero(candidate_finite))
    report["reference_nonfinite"] = int(reference_sum.size - np.count_nonzero(reference_finite))
    if not np.all(candidate_finite) or not np.all(reference_finite):
        report["status"] = "error"
        report["errors"] = ["non-finite species sum"]
        return report

    if candidate_sum.size == 0 or reference_sum.size == 0:
        report["status"] = "error"
        report["errors"] = ["species datasets are empty"]
        return report

    report["candidate_max_abs_sum_minus_one"] = float(np.max(np.abs(candidate_sum - 1.0)))
    report["candidate_rms_sum_minus_one"] = float(np.sqrt(np.mean((candidate_sum - 1.0) ** 2)))
    report["reference_max_abs_sum_minus_one"] = float(np.max(np.abs(reference_sum - 1.0)))
    report["reference_rms_sum_minus_one"] = float(np.sqrt(np.mean((reference_sum - 1.0) ** 2)))

    if candidate_sum.shape != reference_sum.shape:
        report.update(
            status="error",
            errors=[
                f"species-sum shape mismatch: {candidate_sum.shape} versus {reference_sum.shape}"
            ],
        )
        return report

    report["sum_comparison"] = symmetric_norm_metrics(candidate_sum, reference_sum, floor)
    report["status"] = "ok"
    return report


def compare_files(args: argparse.Namespace) -> Tuple[Dict[str, Any], int]:
    report: Dict[str, Any] = {
        "candidate": str(args.candidate.resolve()),
        "reference": str(args.reference.resolve()),
        "rtol": args.rtol,
        "atol": args.atol,
        "scale_floor": args.scale_floor,
        "fields": [],
        "errors": [],
        "warnings": [],
    }
    structural_failure = False
    tolerance_failure = False

    with h5py.File(args.candidate, "r") as candidate_file, h5py.File(args.reference, "r") as reference_file:
        report["candidate_attributes"] = json_value(dict(candidate_file.attrs))
        report["reference_attributes"] = json_value(dict(reference_file.attrs))

        if "Data" not in candidate_file or not isinstance(candidate_file["Data"], h5py.Group):
            raise ValueError(f"{args.candidate}: missing /Data group")
        if "Data" not in reference_file or not isinstance(reference_file["Data"], h5py.Group):
            raise ValueError(f"{args.reference}: missing /Data group")

        candidate_data = numeric_datasets(candidate_file["Data"])
        reference_data = numeric_datasets(reference_file["Data"])
        candidate_names = set(candidate_data)
        reference_names = set(reference_data)
        common_names = sorted(candidate_names & reference_names)
        only_candidate = sorted(candidate_names - reference_names)
        only_reference = sorted(reference_names - candidate_names)
        report["only_candidate"] = only_candidate
        report["only_reference"] = only_reference

        if not common_names:
            structural_failure = True
            report["errors"].append("no common numeric datasets below /Data")
        if (only_candidate or only_reference) and not args.allow_missing:
            structural_failure = True
            report["errors"].append("numeric dataset schemas differ; use --allow-missing to compare the intersection")

        for name in common_names:
            candidate_dataset = candidate_data[name]
            reference_dataset = reference_data[name]
            field: Dict[str, Any] = {
                "name": name,
                "candidate_shape": list(candidate_dataset.shape),
                "reference_shape": list(reference_dataset.shape),
                "candidate_dtype": str(candidate_dataset.dtype),
                "reference_dtype": str(reference_dataset.dtype),
            }
            if candidate_dataset.shape != reference_dataset.shape:
                field["status"] = "shape-mismatch"
                structural_failure = True
                report["fields"].append(field)
                continue

            candidate = np.asarray(candidate_dataset[...], dtype=np.float64)
            reference = np.asarray(reference_dataset[...], dtype=np.float64)
            candidate_nonfinite = int(candidate.size - np.count_nonzero(np.isfinite(candidate)))
            reference_nonfinite = int(reference.size - np.count_nonzero(np.isfinite(reference)))
            field["candidate_nonfinite"] = candidate_nonfinite
            field["reference_nonfinite"] = reference_nonfinite
            if candidate_nonfinite or reference_nonfinite:
                field["status"] = "non-finite"
                structural_failure = True
                report["fields"].append(field)
                continue

            metrics = symmetric_norm_metrics(candidate, reference, args.scale_floor)
            field.update(metrics)
            field["tolerance_pass"] = tolerance_pass(metrics, args.rtol, args.atol)
            field["status"] = "ok"
            if field["tolerance_pass"] is False:
                tolerance_failure = True
            report["fields"].append(field)

        species = species_sum_report(
            candidate_data, reference_data, args.species_prefix, args.scale_floor
        )
        report["species_sum"] = species
        if species["status"] == "error":
            structural_failure = True

    report["structural_pass"] = not structural_failure
    report["tolerance_pass"] = None if args.rtol is None and args.atol is None else not tolerance_failure
    report["pass"] = not structural_failure and not tolerance_failure
    if structural_failure:
        return report, 2
    if tolerance_failure:
        return report, 1
    return report, 0


def print_human(report: Mapping[str, Any]) -> None:
    print(f"candidate: {report['candidate']}")
    print(f"reference: {report['reference']}")
    print(
        f"tolerance: rtol={report['rtol']!r} atol={report['atol']!r} "
        f"scale_floor={report['scale_floor']:.3e}"
    )
    print()
    print(f"{'field':<28} {'max_abs':>12} {'rel_L1':>12} {'rel_L2':>12} {'rel_Linf':>12} {'status':>10}")
    print("-" * 93)
    for field in report["fields"]:
        if field["status"] != "ok":
            print(f"{field['name']:<28} {'-':>12} {'-':>12} {'-':>12} {'-':>12} {field['status']:>10}")
            continue
        tolerance = field["tolerance_pass"]
        status = "report" if tolerance is None else "PASS" if tolerance else "FAIL"
        print(
            f"{field['name']:<28} {field['max_abs']:12.4e} {field['rel_l1']:12.4e} "
            f"{field['rel_l2']:12.4e} {field['rel_linf']:12.4e} {status:>10}"
        )

    species = report["species_sum"]
    print()
    if species["status"] == "ok":
        print(
            "species sum: "
            f"candidate max|sumX-1|={species['candidate_max_abs_sum_minus_one']:.4e}, "
            f"reference max|sumX-1|={species['reference_max_abs_sum_minus_one']:.4e}, "
            f"cross max_abs={species['sum_comparison']['max_abs']:.4e}"
        )
    else:
        print(f"species sum: {species['status']} ({species.get('reason', species.get('errors', ''))})")

    if report["only_candidate"]:
        print("only in candidate: " + ", ".join(report["only_candidate"]))
    if report["only_reference"]:
        print("only in reference: " + ", ".join(report["only_reference"]))
    for error in report["errors"]:
        print(f"ERROR: {error}", file=sys.stderr)
    if report["rtol"] is None and report["atol"] is None and report["structural_pass"]:
        print("overall: VALID (report-only; no numeric tolerance applied)")
    else:
        print(f"overall: {'PASS' if report['pass'] else 'FAIL'}")


def write_json(report: Mapping[str, Any], destination: str) -> None:
    payload = json.dumps(report, indent=2, sort_keys=True, allow_nan=False) + "\n"
    if destination == "-":
        sys.stdout.write(payload)
    else:
        Path(destination).write_text(payload, encoding="utf-8")


def main(argv: Optional[Sequence[str]] = None) -> int:
    args = parse_args(argv)
    try:
        report, status = compare_files(args)
    except (OSError, ValueError, KeyError) as exc:
        print(f"comparison failed: {exc}", file=sys.stderr)
        return 2

    if args.json_output != "-":
        print_human(report)
    if args.json_output:
        write_json(report, args.json_output)
    return status


if __name__ == "__main__":
    raise SystemExit(main())
