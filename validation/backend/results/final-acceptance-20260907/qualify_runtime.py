"""Archive the existing full-runtime qualifier for this frozen candidate.

This only invokes the maintained validator. Its release_qualified=false result
is retained: scientific, diagnostic and resource gates have separate records.
"""
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / 'tools'))
from validate_backend_results import require_empty_output_root, run_arch_with_logs


def main():
    build = ROOT / 'build/release-core-throughput-cmake'
    archive = Path(__file__).resolve().parent / 'release-73a9cf50'
    output = archive / 'runtime-qualification'
    require_empty_output_root(output)
    output.mkdir(parents=True, exist_ok=True)
    reports = {
        '--matrix': 'validation/amr/results/cartesian-native-20260907/release-872/backend-validation-evidence.json',
        '--curved-matrix': 'validation/amr/results/curved-native-20260907/release-873/backend-validation-evidence.json',
        '--uniform-matrix': 'validation/backend/results/uniform-native-20260907/release-874/backend-validation-evidence.json',
        '--generated-matrix': 'validation/network/results/runtime-native-20260907/release-875/backend-validation-evidence.json',
        '--restart-smooth': 'validation/amr/results/restart-smooth-native-20260907/release-876/restart-validation-evidence.json',
        '--restart-enuc': 'validation/amr/results/restart-burn-native-20260907/release-877/restart-validation-evidence.json',
    }
    command = [sys.executable, '-B', str(ROOT / 'tools/qualify_cuda_amr_evidence.py'),
        '--profile', 'full-runtime', '--arch', str(build / 'bin/ARCH'),
        '--checkpoint-validator', str(build / 'arch_cuda_single_level_validation'),
        '--source-root', str(ROOT), '--build-dir', str(build), '--configuration', 'Release',
        '--final-artifacts', str(archive / 'final-artifacts.sha256')]
    for option, path in reports.items():
        command.extend((option, str(ROOT / path)))
    result = run_arch_with_logs(command, source_root=ROOT, lane_root=output, timeout=1800)
    if result.returncode:
        raise RuntimeError('full-runtime qualification failed; original logs retained')
    report = json.loads(result.stdout)
    print(json.dumps({key: report[key] for key in (
        'status', 'qualification_scope', 'release_qualified', 'matrices',
        'matrix_cases', 'restart_suites')}, indent=2))


if __name__ == '__main__':
    main()
