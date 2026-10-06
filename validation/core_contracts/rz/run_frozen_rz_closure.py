"""Execute the frozen private CPU RZ source subset; preserve raw data locally.

Frozen contract: CORE-RZ-20261006-v1 / runner-hardening-v1.

Hardened checks implemented here (no scientific C++ / tolerance / budget change):
  * ``--identity`` JSON carrying ``elf_sha256``/``fixture_sha256``/``patch_sha256``
    (each 64 lowercase hex) is required and compared against the actual
    executable, ``tests/host/grid/test_curvilinear_metrics.cpp`` and
    ``validation/gravity/candidates/rz-external-production-binding-20261006.patch``
    BEFORE anything is launched, and re-checked after every execution.
  * the (direction, open, external_phi, method) product must be exactly the 24
    frozen cases for both evolution and restart tables; duplicates, missing rows
    and evolution/restart mismatch are rejected.
  * ``inner=1`` and ``steps=10`` are enforced whenever present; error metrics
    must be finite, non-negative and <= 1e-12.
  * restart rows must show split_time=5e-4, final_time=1e-3, split_step=5,
    final_step=10, bit_words=10240 and a 64 lowercase hex checkpoint_sha.
  * the owner probe must print exactly one RZ_PRODUCTION_OWNER_PASS row with
    rejects=11; the rejection probe exactly one RZ_NUMERICAL_REJECTION_PASS row
    for EACH of Euler/RK2/RK3.
  * the axis failure is preserved exactly as FAILED_ORIGINAL_AXIS_RHS_GATE with
    exit 2 and RZ_EQUILIBRIUM_SPATIAL_GATE=NOT_CLEARED.

Recorded hashes are observed values. The reported base/contract revision labels
are carried over from the reviewed snapshot and are NOT independently verified by
this runner; diagnostics counts alone cannot claim actual scientific
qualification.
"""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import re
import subprocess
import time

ROOT = Path(__file__).resolve().parents[3]
IDENTITY_KEYS = ("elf_sha256", "fixture_sha256", "patch_sha256")
SHA256_RE = re.compile(r"[0-9a-f]{64}")
PATCH_RELATIVE = "validation/gravity/candidates/rz-external-production-binding-20261006.patch"
FIXTURE_RELATIVE = "tests/host/grid/test_curvilinear_metrics.cpp"
DIRECTIONS = ("0", "1")
OPEN_FLAGS = ("0", "1")
PHI_TOKENS = ("-0.025", "0.025")
PHI_MAGNITUDE = 0.025
METHODS = ("Euler", "RK2", "RK3")
INNER = 1
STEPS = 10
ERROR_KEYS = ("J_error", "mass_error", "E_error", "species_error")
RESTART_TIME_FIELDS = {"split_time": 5e-4, "final_time": 1e-3}
RESTART_INTEGER_FIELDS = {"split_step": 5, "final_step": 10, "bit_words": 10240}
RESTART_EXPECTED = {**RESTART_TIME_FIELDS, **RESTART_INTEGER_FIELDS}
INTEGER_TOKEN_RE = re.compile(r"[+-]?[0-9]+")
OWNER_REJECTS = 11
AXIS_NOT_CLEARED = "NOT_CLEARED"
AXIS_STATUS = "FAILED_ORIGINAL_AXIS_RHS_GATE"
PUBLIC_GATES = "NOT_CHECKED_BY_RUNNER"
NAME_OWNER = "owner"
NAME_REJECTION = "numerical-rejection"
NAME_FROZEN = "frozen-rk-checkpoint"
NAME_REGRESSION = "curvilinear-regression"
NAME_AXIS = "axis-rhs"
DIAGNOSTIC_PREFIXES = ("RZ_PRODUCTION_OWNER_PASS", "RZ_NUMERICAL_REJECTION_PASS",
                       "RZ_EQUILIBRIUM_SPATIAL_GATE")
EXPECTED_PRODUCT = frozenset(
    (direction, open_flag, phi, method)
    for direction in DIRECTIONS
    for open_flag in OPEN_FLAGS
    for phi in PHI_TOKENS
    for method in METHODS
)
QUALIFICATION_SCOPE = {
    "evidence": "diagnostics_and_observed_hashes_only",
    "actual_scientific_qualification_claimed": False,
    "reported_base_and_contract_labels_independently_verified": False,
    "note": ("diagnostic counts and source-level checks are recorded evidence; they do not "
             "by themselves establish actual scientific qualification"),
}


class ContractError(RuntimeError):
    """Raised when frozen evidence does not match the frozen contract."""


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def fields(line):
    return dict(re.findall(r"(\w+)=(.*?)(?= \w+=|$)", line))


def rows_for(text, prefix):
    return [fields(line) for line in text.splitlines() if line.startswith(prefix)]


def read_identity(argument):
    """Return the declared identity mapping from inline JSON or a JSON file path.

    Explicit inline JSON is parsed BEFORE any filesystem access: a realistic
    inline blob (three 64 hex hashes plus keys/newlines is already > 255
    characters) must never be interpreted as an over-long path token.
    Unusable identity paths surface as ``ContractError``, never ``OSError``.
    """
    text = str(argument)
    if text.lstrip().startswith("{"):
        try:
            payload = json.loads(text)
        except json.JSONDecodeError as error:
            raise ContractError("identity inline JSON is invalid: " + str(error)) from error
    else:
        path = Path(text)
        try:
            is_file = path.is_file()
        except OSError as error:
            raise ContractError("identity path is not readable: " + str(error)) from error
        if not is_file:
            raise ContractError("identity is neither inline JSON nor a readable JSON file: "
                                + repr(text))
        try:
            payload = json.loads(path.read_text())
        except (OSError, json.JSONDecodeError) as error:
            raise ContractError("identity JSON file is not readable: " + str(error)) from error
    if not isinstance(payload, dict):
        raise ContractError("identity must be a JSON object")
    for key in IDENTITY_KEYS:
        value = payload.get(key)
        if not isinstance(value, str) or not SHA256_RE.fullmatch(value):
            raise ContractError("identity " + key + " must be 64 lowercase hex")
    return {key: payload[key] for key in IDENTITY_KEYS}


def guarded_paths(source, build):
    """Return the identity-guarded artifacts as a key -> path mapping."""
    return {
        "elf": Path(build) / "arch_curvilinear_metrics",
        "fixture": Path(source) / FIXTURE_RELATIVE,
        "patch": ROOT / PATCH_RELATIVE,
    }


def observe_identity(paths):
    observed = {}
    for key, path in paths.items():
        if not Path(path).is_file():
            raise ContractError("identity artifact missing: " + str(path))
        observed[key + "_sha256"] = digest(path)
    return observed


def verify_identity(identity, paths, label="preflight"):
    observed = observe_identity(paths)
    for key in IDENTITY_KEYS:
        if observed[key] != identity[key]:
            raise ContractError(label + ": " + key + " mismatch: declared " + identity[key]
                                + " observed " + observed[key])
    return observed


def _first(row, names):
    for name in names:
        if name in row:
            return name, row[name]
    return None, None


def canonical_direction(row):
    _, value = _first(row, ("direction", "dir", "interface_direction"))
    if value is None:
        raise ContractError("product row is missing direction")
    text = value.strip()
    if text not in DIRECTIONS:
        raise ContractError("direction must be 0/1, got " + repr(value))
    return text


def canonical_open(row):
    _, value = _first(row, ("open", "open_boundary"))
    if value is None:
        raise ContractError("product row is missing open")
    text = value.strip().lower()
    if text in ("0", "false", "no"):
        return "0"
    if text in ("1", "true", "yes"):
        return "1"
    raise ContractError("open must be 0/1, got " + repr(value))


def canonical_phi(row):
    _, value = _first(row, ("external_phi", "phi", "g_phi"))
    if value is None:
        raise ContractError("product row is missing external_phi")
    try:
        number = float(value)
    except (TypeError, ValueError):
        raise ContractError("external_phi must be +/-0.025, got " + repr(value)) from None
    # Exact frozen-float equality: no invented tolerance may widen the frozen input.
    if number == PHI_MAGNITUDE:
        return "0.025"
    if number == -PHI_MAGNITUDE:
        return "-0.025"
    raise ContractError("external_phi must equal float(+/-0.025), got " + repr(value))


def canonical_method(row):
    _, value = _first(row, ("method", "scheme", "solver"))
    if value is None:
        raise ContractError("product row is missing method")
    text = value.strip()
    for method in METHODS:
        if text.lower() == method.lower():
            return method
    raise ContractError("method must be one of " + repr(METHODS) + ", got " + repr(value))


def product_key(row):
    return (canonical_direction(row), canonical_open(row), canonical_phi(row),
            canonical_method(row))


def product_rows(rows, label):
    """Map each unique product key to its row, rejecting duplicates."""
    cases = {}
    for index, row in enumerate(rows):
        key = product_key(row)
        if key in cases:
            raise ContractError(label + ": duplicate product case " + repr(key)
                                + " at row " + str(index))
        cases[key] = row
    return cases


def _require_complete(cases, label):
    missing = sorted(EXPECTED_PRODUCT - set(cases))
    unexpected = sorted(set(cases) - EXPECTED_PRODUCT)
    if missing or unexpected:
        raise ContractError(label + ": product mismatch; missing=" + repr(missing)
                            + " unexpected=" + repr(unexpected))


def _check_inner_steps(row, label):
    name, value = _first(row, ("inner",))
    if name is not None and value.strip() != str(INNER):
        raise ContractError(label + ": inner must be " + str(INNER) + ", got " + repr(value))
    name, value = _first(row, ("steps",))
    if name is not None and value.strip() != str(STEPS):
        raise ContractError(label + ": steps must be " + str(STEPS) + ", got " + repr(value))


def check_error_metric(row, key):
    if key not in row:
        raise ContractError("error metric missing: " + key)
    text = row[key].strip()
    try:
        value = float(text)
    except ValueError:
        raise ContractError(key + " must be numeric, got " + repr(row[key])) from None
    if not math.isfinite(value):
        raise ContractError(key + " must be finite, got " + repr(row[key]))
    if value < 0:
        raise ContractError(key + " must be non-negative, got " + repr(row[key]))
    if value > 1e-12:
        raise ContractError(key + " exceeds 1e-12, got " + repr(row[key]))
    return value


def validate_evolution_rows(rows):
    cases = product_rows(rows, "evolution")
    _require_complete(cases, "evolution")
    for key, row in cases.items():
        _check_inner_steps(row, "evolution")
        for error_key in ERROR_KEYS:
            check_error_metric(row, error_key)
    return cases


def validate_restart_rows(rows, evolution):
    cases = product_rows(rows, "restart")
    _require_complete(cases, "restart")
    if set(cases) != set(evolution):
        raise ContractError("evolution/restart product mismatch")
    for key, row in cases.items():
        # Frozen time endpoints must match the frozen floats exactly; no 1e-15 slack.
        for field, expected in RESTART_TIME_FIELDS.items():
            if field not in row:
                raise ContractError("restart row " + repr(key) + " missing " + field)
            text = row[field].strip()
            try:
                number = float(text)
            except ValueError:
                raise ContractError("restart " + field + " must be numeric, got "
                                    + repr(row[field])) from None
            if not math.isfinite(number) or number != expected:
                raise ContractError("restart " + field + " must equal the frozen float "
                                    + repr(expected) + ", got " + repr(row[field]))
        # Steps and bit_words must be strict integer tokens, not fractional near misses.
        for field, expected in RESTART_INTEGER_FIELDS.items():
            if field not in row:
                raise ContractError("restart row " + repr(key) + " missing " + field)
            text = row[field].strip()
            if not INTEGER_TOKEN_RE.fullmatch(text) or int(text) != expected:
                raise ContractError("restart " + field + " must be the strict integer token "
                                    + str(expected) + ", got " + repr(row[field]))
        checkpoint = row.get("checkpoint_sha", "")
        if not SHA256_RE.fullmatch(checkpoint.strip()):
            raise ContractError("restart checkpoint_sha must be 64 lowercase hex, got "
                                + repr(checkpoint))
    return cases


def validate_owner_rows(rows):
    if len(rows) != 1:
        raise ContractError("owner PASS rows must be exactly one, got " + str(len(rows)))
    row = rows[0]
    value = row.get("rejects")
    if value is None or value.strip() != str(OWNER_REJECTS):
        raise ContractError("owner rejects must be exactly " + str(OWNER_REJECTS)
                            + ", got " + repr(value))
    return row


def validate_rejection_rows(rows):
    if len(rows) != len(METHODS):
        raise ContractError("numerical rejection rows must be exactly one per "
                            + repr(METHODS) + ", got " + str(len(rows)))
    methods = [canonical_method(row) for row in rows]
    if sorted(methods) != sorted(METHODS):
        raise ContractError("numerical rejection methods must be exactly " + repr(METHODS)
                            + ", got " + repr(methods))
    return rows


def validate_axis_failure(exit_code, text, rows):
    if exit_code != 2:
        raise ContractError("axis-rhs gate exit must be exactly 2, got " + repr(exit_code))
    if "RZ_EQUILIBRIUM_SPATIAL_GATE=" + AXIS_NOT_CLEARED not in text:
        raise ContractError("axis failure reason changed; review required")
    return rows


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--identity", required=True,
                        help="JSON file or inline JSON with "
                             "elf_sha256/fixture_sha256/patch_sha256")
    args = parser.parse_args()
    source, build, output = args.source.resolve(), args.build.resolve(), args.output.resolve()
    owned = (ROOT / "studio/.local/integration").resolve()
    if any(not path.is_relative_to(owned) for path in (source, build, output)) or output.exists():
        raise RuntimeError("private source/build and new local output required")
    if "CMAKE_HOME_DIRECTORY:INTERNAL=" + str(source) not in (build / "CMakeCache.txt").read_text():
        raise RuntimeError("source/build identity mismatch")
    identity = read_identity(args.identity)
    paths = guarded_paths(source, build)
    verify_identity(identity, paths, "preflight")
    output.mkdir(parents=True)
    elf = paths["elf"]
    records = []
    for name, arguments, expected in [
        (NAME_OWNER, ["rz-source-owner-audit"], 0),
        (NAME_REJECTION, ["rz-source-rejection-audit"], 0),
        (NAME_FROZEN, ["rz-applied-torque-restart-audit", str(output / "checkpoints")], 0),
        (NAME_REGRESSION, [], 0),
        (NAME_AXIS, ["rz-equilibrium-audit"], 2),
    ]:
        started = time.monotonic()
        run = subprocess.run([str(elf), *arguments], capture_output=True, text=True, timeout=120,
                             env={**os.environ, "OMP_NUM_THREADS": "1"})
        (output / (name + ".log")).write_text(run.stdout + run.stderr)
        record = {"name": name, "exit_code": run.returncode,
                  "elapsed_seconds": time.monotonic() - started,
                  "status": AXIS_STATUS if name == NAME_AXIS else "PASS",
                  "diagnostics": [line for line in run.stdout.splitlines()
                      if line.startswith(DIAGNOSTIC_PREFIXES)]}
        records.append(record)
        if run.returncode != expected:
            record["status"] = "FAILED"
            (output / "partial-summary.json").write_text(json.dumps(records, indent=2) + "\n")
            raise RuntimeError(name + ": failed; do not continue or relabel")
        if name == NAME_OWNER:
            record["owner"] = validate_owner_rows(rows_for(run.stdout, "RZ_PRODUCTION_OWNER_PASS"))
        if name == NAME_REJECTION:
            record["rejection"] = validate_rejection_rows(
                rows_for(run.stdout, "RZ_NUMERICAL_REJECTION_PASS"))
        if name == NAME_FROZEN:
            evolution = validate_evolution_rows(rows_for(run.stdout, "RZ_ROTATING_BUDGET"))
            restart = validate_restart_rows(rows_for(run.stdout, "RZ_FROZEN_CHECKPOINT_PASS"),
                                            evolution)
            record.update({"evolution": list(evolution.values()), "restart": list(restart.values()),
                "maximum_errors": {key: max(float(row[key]) for row in evolution.values())
                    for key in ERROR_KEYS}})
        if name == NAME_AXIS:
            record["rows"] = validate_axis_failure(
                run.returncode, run.stdout, rows_for(run.stdout, "RZ_EQUILIBRIUM inner="))
        verify_identity(identity, paths, "post-run " + name)
        print(name + " " + record["status"], flush=True)
    observed = verify_identity(identity, paths, "pre-summary")
    summary = {
        "status": "PRIVATE_EXTERNAL_SUBSET_PASS_FULL_RZ_NOT_SIGNED",
        "delivery_base": "1f743efd7cf7d0793a766f3c2cd4fdca1bc9cadd",
        "snapshot_base": "326626cb1f51dc82ad78c5d59238b07e84d1cbbc",
        "core_contract": "4639774fe3c94ae27b3e831d0f0b9d6340de34fd",
        "closure_ref": "b466ce0216928fee56878afe43aae9e8a1614f25",
        "elf_sha256": observed["elf_sha256"], "fixture_sha256": observed["fixture_sha256"],
        "patch_sha256": observed["patch_sha256"],
        "identity_preflight": "MATCHED_AND_REVALIDATED",
        "qualification_scope": dict(QUALIFICATION_SCOPE),
        "frozen_input": {"r": [1, 3], "z": [-1, 1], "dt": 1e-4, "steps": STEPS,
            "g_phi": [-PHI_MAGNITUDE, PHI_MAGNITUDE], "schemes": list(METHODS),
            "interface_directions": [0, 1], "open_boundary": [False, True],
            "field_count": 5, "species_count": 2, "leaves": 5},
        "public_gates": PUBLIC_GATES, "records": records,
        "raw_output_root": str(output), "raw_output_bytes": sum(
            f.stat().st_size for f in output.rglob("*") if f.is_file()),
        "benchmark_2d": {"status": "NOT_RUN", "reason":
            "physical-core/type association and owned allocation/next-write resource guards are not established"},
    }
    (output / "summary.json").write_text(json.dumps(summary, indent=2) + "\n")


if __name__ == "__main__":
    main()
