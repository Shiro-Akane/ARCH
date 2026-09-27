#!/usr/bin/env python3
"""Record the P2 CPU prototype's fixed analytic tests and narrow source identity."""
import argparse
import csv
import io
import json
from pathlib import Path
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
import validation_provenance as provenance
from validate_backend_results import require_empty_output_root, run_arch_with_logs

SOURCES = [
    'src/numerics/elliptic/CartesianPoisson.h', 'src/numerics/elliptic/CartesianPoisson.cpp',
    'src/numerics/multigrid/HostMultigrid.h', 'src/numerics/multigrid/HostMultigrid.cpp',
    'src/numerics/multigrid/MGTransfer.h', 'src/numerics/linalg/DenseWrap.h',
    'src/physics/gravity/UniformGravity.h', 'src/physics/gravity/UniformGravity.cpp',
    'src/physics/constant/PhysicalConstants.h', 'src/grid/ScalarFieldView.h',
    'src/core/CompensatedSum.h', 'src/core/ArchPortability.h',
    'tests/host/gravity/test_poisson_multigrid.cpp', 'cmake/tests/HostTests.cmake',
    'validation/gravity/p2_cpu_poisson.py', 'tools/validation_provenance.py',
    'tools/validate_backend_results.py', 'tools/validation_sanitizer.py',
]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', required=True, type=Path)
    parser.add_argument('--output-dir', required=True, type=Path)
    args = parser.parse_args()
    build, output = args.build_dir.resolve(), args.output_dir.resolve()
    executable = build / 'arch_poisson_multigrid'
    if not executable.is_file():
        parser.error(f'build the arch_poisson_multigrid target first: {executable}')
    cache = (build / 'CMakeCache.txt').read_text().splitlines()
    if f'CMAKE_HOME_DIRECTORY:INTERNAL={ROOT}' not in cache:
        parser.error('the CMake build belongs to a different source directory')
    require_empty_output_root(output)
    output.mkdir(parents=True, exist_ok=True)
    sources = {name: provenance.file_identity(ROOT / name) for name in SOURCES}
    binary_identity = provenance.file_identity(executable)
    keys = ('CMAKE_BUILD_TYPE:', 'CMAKE_CXX_COMPILER:', 'ARCH_ENABLE_CUDA:', 'ARCH_ENABLE_KLU:')
    report = {
        'scope': 'P2 single-domain CPU Poisson; no production self route or GPU execution',
        'base_commit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
        'candidate_sources': sources,
        'executable': binary_identity,
        'build_cache': [line for line in cache if line.startswith(keys)],
        'tests': [],
    }
    for mode in ('contract', 'analytic'):
        started = time.monotonic()
        command = [str(executable), mode]
        lane = output / mode
        lane.mkdir()
        run = run_arch_with_logs(command, source_root=ROOT, lane_root=lane, timeout=600)
        report['tests'].append({'command': command, 'returncode': run.returncode,
                                'elapsed_seconds': time.monotonic()-started})
        if run.returncode != 0 or f'P2 {mode} validation passed' not in run.stdout:
            (output / 'report.json').write_text(json.dumps(report, indent=2)+'\n')
            raise SystemExit(f'P2 {mode} failed; see {output}')
        if mode == 'analytic':
            convergence, cgs = run.stdout.split('rho0,length,', 1)
            cgs = 'rho0,length,' + cgs.split('P2 analytic validation passed', 1)[0]
            (output / 'convergence.csv').write_text(convergence)
            (output / 'cgs.csv').write_text(cgs)
            report['convergence'] = list(csv.DictReader(io.StringIO(convergence)))
            report['cgs'] = list(csv.DictReader(io.StringIO(cgs)))
    if sources != {name: provenance.file_identity(ROOT / name) for name in SOURCES} \
            or binary_identity != provenance.file_identity(executable):
        raise SystemExit('source or executable changed during verification; evidence is not accepted')
    report['status'] = 'passed'
    (output / 'report.json').write_text(json.dumps(report, indent=2)+'\n')
    print(f'P2 CPU contract and analytic verification passed: {output}')


if __name__ == '__main__':
    main()
