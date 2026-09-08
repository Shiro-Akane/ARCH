"""Archive the complete configured CTest inventory and its actual artifacts.

Run under the existing memory guard. A skipped, missing or failed test is not
a release-profile pass, even when CTest itself exits successfully.
"""
import argparse
from datetime import datetime, timezone
import json
import math
from pathlib import Path
import shutil
import sys
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / "tools"))
import validation_provenance as provenance
from validate_backend_results import require_empty_output_root, run_arch_with_logs


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--expected-tests', type=int, required=True)
    args = parser.parse_args()
    build, output = args.build_dir.resolve(), args.output_dir.resolve()
    require_empty_output_root(output)
    output.mkdir(parents=True, exist_ok=True)
    query = run_arch_with_logs(['ctest', '--test-dir', str(build), '--show-only=json-v1'],
                              source_root=ROOT, lane_root=output, timeout=60)
    if query.returncode:
        raise RuntimeError('cannot inspect CTest inventory')
    inventory = json.loads(query.stdout)
    (output / 'ctest-inventory.json').write_text(json.dumps(inventory, indent=2) + '\n')
    tests = inventory['tests']
    if len(tests) != args.expected_tests or args.expected_tests < 1 \
            or len({test['name'] for test in tests}) != len(tests):
        raise RuntimeError('configured release test inventory differs')
    executables = {test['name']: Path(test['command'][0]).resolve() for test in tests}
    artifacts = {name: path for name, path in executables.items() if path.is_relative_to(build)}
    artifacts['arch'] = build / 'bin/ARCH'
    test_tools = {name: path for name, path in executables.items() if not path.is_relative_to(build)}
    test_tools['ctest'] = Path(shutil.which('ctest')).resolve()
    def tool_identity():
        return {name: provenance.file_identity(path) for name, path in test_tools.items()}
    def identity():
        return provenance.capture_focused(artifacts=artifacts, source_root=ROOT, build_dir=build)
    before, recipe = identity(), provenance.file_identity(Path(__file__).resolve())
    tools = tool_identity()
    started = datetime.now(timezone.utc).isoformat()
    command = ['ctest', '--test-dir', str(build), '--output-on-failure', '--no-tests=error',
               '--parallel', '1', '--output-junit', str(output / 'ctest.xml')]
    result = run_arch_with_logs(command, source_root=ROOT, lane_root=output, timeout=7200)
    if result.returncode:
        raise RuntimeError(f'complete CTest run failed: {result.returncode}; logs retained')
    cases = ET.parse(output / 'ctest.xml').getroot().findall('.//testcase')
    if sorted(case.attrib['name'] for case in cases) != sorted(test['name'] for test in tests) \
            or any(case.find(tag) is not None for case in cases for tag in ('failure', 'error', 'skipped')) \
            or any(not math.isfinite(float(case.attrib['time'])) or float(case.attrib['time']) < 0 for case in cases):
        raise RuntimeError('CTest contains missing, skipped or failed cases')
    provenance.require_unchanged(before, identity())
    if tools != tool_identity():
        raise RuntimeError('CTest or a test interpreter changed during execution')
    if recipe != provenance.file_identity(Path(__file__).resolve()):
        raise RuntimeError('regression recipe changed during execution')
    evidence = dict(schema=1, scope='complete-configured-cpu-cuda-regression',
        focused_gate_pass=True, release_qualified=False, identity=before,
        identity_verified_after_run=True, recipe=recipe, command=command, test_tools=tools,
        started_utc=started, finished_utc=datetime.now(timezone.utc).isoformat(),
        test_count=len(tests), inventory=provenance.file_identity(output / 'ctest-inventory.json'),
        junit=provenance.file_identity(output / 'ctest.xml'),
        stdout=provenance.file_identity(output / 'arch.stdout'),
        stderr=provenance.file_identity(output / 'arch.stderr'))
    (output / 'evidence.json').write_text(json.dumps(evidence, indent=2) + '\n')
    print(f'complete CTest PASS: {len(tests)} tests; {output / "evidence.json"}')


if __name__ == '__main__':
    main()
