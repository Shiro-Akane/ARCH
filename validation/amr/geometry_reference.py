"""Verify independent metric data using high-precision defining integrals.

Run with Python 3 + mpmath. Reads only test input/reference literals; does not
import production mathematics, update fixtures or derive expected data from
ARCH. Both precision evaluations must round identically before comparison.
This is a local measure oracle, not full hydro/diffusion/AMR qualification.
"""

from pathlib import Path
import argparse
from datetime import datetime, timezone
import json
import math
import re
import sys

ROOT = Path(__file__).resolve().parents[2]


def integral_reference(inputs, digits):
    import mpmath as mp
    radius, width, theta, angular_width = inputs
    with mp.workdps(digits):
        left, right = mp.mpf(radius), mp.mpf(radius + width)
        theta_left, theta_right = mp.mpf(theta), mp.mpf(theta + angular_width)
        return [float((right**3 - left**3) / 3),
                float((right**2 - left**2) / 2),
                float(mp.cos(theta_left) - mp.cos(theta_right))]


def check_metric_references():
    path = Path(__file__).resolve().parents[2] / "tests/math/CurvilinearMetricCases.h"
    source = path.read_text()
    data = source.split("// BEGIN INDEPENDENT MEASURE DATA", 1)[1].split(
        "// END INDEPENDENT MEASURE DATA", 1)[0]
    rows = re.findall(r"\{([^{}]+)\},", data)
    if not rows:
        raise ValueError("missing independent metric cases")
    for index, row in enumerate(rows):
        values = [float.fromhex(token.strip()) if "0x" in token else float(token)
                  for token in row.split(",")]
        if len(values) != 7:
            raise ValueError(f"invalid metric row {index}")
        low, high = (integral_reference(values[:4], digits) for digits in (70, 90))
        if low != high or high != values[4:]:
            raise ValueError(f"independent metric reference mismatch at row {index}")
    return dict(status="pass", cases=len(rows), precisions=[70, 90])


def viscous_transcript(text, backend):
    """Require the complete spatial matrix, including null and variable-mu fields."""
    records = {}
    for line in text.splitlines():
        if not line.startswith('VISCOUS_SPATIAL_CONVERGENCE '):
            continue
        values = dict(field.split('=', 1) for field in line.split()[1:])
        key = (values['geometry'], int(values['dim']), int(values['uniform']),
               float(values['density_slope']), float(values['h']))
        error = float(values['error'])
        if values['backend'] != backend or key in records or not math.isfinite(error) or error < 0:
            raise ValueError('invalid/duplicate viscous convergence record')
        records[key] = error
    cases = {(geometry, dimension, uniform, slope)
             for geometry in ('cartesian', 'cylindrical', 'spherical')
             for dimension in (1, 2, 3) for slope in (0., .1)
             for uniform in ((0,) if dimension == 1 else (0, 1))}
    spacings = (.05, .025, .0125)
    if set(records) != {(*case, spacing) for case in cases for spacing in spacings}:
        raise ValueError('incomplete viscous geometry/dimension/profile/refinement coverage')
    for case in cases:
        errors = [records[(*case, spacing)] for spacing in spacings]
        if errors[-1] > 1.e-4 or any(coarse > 1.e-10 and coarse < 3.5*fine
                                  for coarse, fine in zip(errors, errors[1:])):
            raise ValueError('viscous analytic error or convergence budget failed')
    return dict(samples=len(records), cases=len(cases), spacings=spacings,
                finest_error_budget=1.e-4, minimum_refinement_factor=3.5,
                maximum_finest_error=max(records[(*case, spacings[-1])] for case in cases))


def origin_transcript(text, backend):
    """Null-field and actual-matrix contraction controls, not just parity."""
    origins, stability, density = {}, {}, {}
    for line in text.splitlines():
        tag, _, payload = line.partition(' ')
        if tag not in ('VISCOUS_ORIGIN', 'VISCOUS_RADIAL_STABILITY', 'VISCOUS_DENSITY_STABILITY'):
            continue
        values = dict(field.split('=', 1) for field in payload.split())
        if values['backend'] != backend:
            raise ValueError('wrong origin backend')
        key = (values['geometry'], float(values['contrast'] if tag == 'VISCOUS_DENSITY_STABILITY' else values['h']))
        if tag == 'VISCOUS_ORIGIN':
            key = (*key, int(values['dim']))
            measured = float(values['error'])
            if key in origins or not 0 <= measured <= 2.e-11:
                raise ValueError('invalid/duplicate origin balance')
            origins[key] = measured
        else:
            target = density if tag == 'VISCOUS_DENSITY_STABILITY' else stability
            measured = (float(values['minimum_entry']), float(values['maximum_row_sum']))
            if (key in target or not all(math.isfinite(v) for v in measured)
                    or measured[0] < -2.e-12 or measured[1] > 1.+2.e-12):
                raise ValueError('invalid/duplicate radial contraction')
            target[key] = measured
    radial_cases = {(geometry, spacing) for geometry in ('cylindrical', 'spherical')
                    for spacing in (.025, .0125, .00625)}
    if (set(stability) != radial_cases
            or set(density) != {(geometry, contrast)
                for geometry in ('cartesian', 'cylindrical', 'spherical') for contrast in (1., 10., 100.)}
            or set(origins) != {(*case, dim) for case in radial_cases for dim in (1, 2, 3)}):
        raise ValueError('incomplete origin/stability coverage')
    return dict(origin_samples=len(origins), contraction_matrices=len(stability),
                density_contraction_matrices=len(density),
                density_minimum_euler_entry=min(v[0] for v in density.values()),
                density_maximum_euler_row_sum=max(v[1] for v in density.values()),
                maximum_null_error=max(origins.values()),
                minimum_euler_entry=min(v[0] for v in stability.values()),
                maximum_euler_row_sum=max(v[1] for v in stability.values()))


def scalar_transcript(text):
    """The CUDA executable evaluates both independent Host and Device scalars."""
    records = {}
    for line in text.splitlines():
        tag, _, payload = line.partition(' ')
        if tag not in ('DIFFUSION_SPATIAL_CONVERGENCE', 'THERMAL_SPATIAL_CONVERGENCE'):
            continue
        values = dict(field.split('=', 1) for field in payload.split())
        key = (tag, values['geometry'], int(values['dim']), float(values['h']))
        errors = (float(values['host_error']), float(values['device_error']))
        if key in records or not all(math.isfinite(v) and v >= 0 for v in errors):
            raise ValueError('invalid/duplicate scalar convergence')
        records[key] = errors
    cases = {(tag, geometry, dim)
             for tag in ('DIFFUSION_SPATIAL_CONVERGENCE', 'THERMAL_SPATIAL_CONVERGENCE')
             for geometry in ('cartesian', 'cylindrical', 'spherical') for dim in (1, 2, 3)}
    spacings = (.05, .025, .0125)
    if set(records) != {(*case, h) for case in cases for h in spacings}:
        raise ValueError('incomplete thermal/species refinement coverage')
    for case in cases:
        for backend in (0, 1):
            errors = [records[(*case, h)][backend] for h in spacings]
            if errors[-1] > 1.e-6 or any(a < 3.5*b for a, b in zip(errors, errors[1:])):
                raise ValueError('scalar convergence budget failed')
    return dict(samples=len(records), backend_evaluations=2*len(records),
                minimum_refinement_factor=3.5, finest_error_budget=1.e-6,
                maximum_finest_error=max(max(records[(*case, spacings[-1])]) for case in cases))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path)
    parser.add_argument('--output-dir', type=Path)
    args = parser.parse_args()
    if (args.build_dir is None) != (args.output_dir is None):
        parser.error('--build-dir and --output-dir are required together')
    references = check_metric_references()
    if args.build_dir is None:
        print(json.dumps({**references, 'release_qualified': False}))
        return
    sys.path.insert(0, str(ROOT / 'tools'))
    import validation_provenance as provenance
    from validate_backend_results import require_empty_output_root, run_arch_with_logs
    build, output = args.build_dir.resolve(), args.output_dir.resolve()
    artifacts = dict(cpu=build/'arch_curvilinear_metrics',
                     cuda=build/'arch_cuda_curvilinear_geometry_smoke')
    def identity():
        return provenance.capture_focused(artifacts=artifacts, source_root=ROOT, build_dir=build)
    before = identity()
    require_empty_output_root(output)
    output.mkdir(parents=True, exist_ok=True)
    started = datetime.now(timezone.utc).isoformat()
    summaries, origins, transcripts, commands = {}, {}, {}, {}
    for backend, executable in artifacts.items():
        lane = output / backend
        lane.mkdir()
        commands[backend] = [str(executable)]
        result = run_arch_with_logs(commands[backend], source_root=ROOT, lane_root=lane, timeout=120)
        if result.returncode != 0:
            raise RuntimeError(f'{backend} geometry test failed: {result.returncode}; logs: {lane}')
        transcript = lane/'arch.stdout'
        text = transcript.read_text()
        summaries[backend] = viscous_transcript(text, backend)
        origins[backend] = origin_transcript(text, backend)
        if backend == 'cuda':
            scalars = scalar_transcript(text)
        transcripts[backend] = provenance.file_identity(transcript)
    provenance.require_unchanged(before, identity())
    evidence = dict(schema=2, scope='independent-metrics-and-diffusion-spatial-operators',
        focused_gate_pass=True, release_qualified=False, identity=before,
        identity_verified_after_run=True, started_utc=started,
        finished_utc=datetime.now(timezone.utc).isoformat(), commands=commands,
        reference_command=[sys.executable, '-B', str(Path(__file__).resolve())],
        metric_references=references, viscous=summaries, radial_origin=origins,
        scalars=scalars, transcripts=transcripts,
        coverage_note='Independent spatial consistency and existing focused geometry/parity tests; '
                      'time-evolved coupled AMR and final application qualification are separate.')
    (output/'evidence.json').write_text(json.dumps(evidence, indent=2, sort_keys=True)+'\n')
    print(json.dumps(dict(focused_gate_pass=True, release_qualified=False, evidence=str(output/'evidence.json'))))


if __name__ == "__main__":
    main()
