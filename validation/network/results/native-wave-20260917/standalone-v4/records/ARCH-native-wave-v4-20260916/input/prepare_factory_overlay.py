"""Prepare an isolated CMake overlay ONLY after real standalone contracts pass.

This does not compile a factory or ARCH, and never changes a production tree.
The resulting overlay must be applied to a new canonical source tree and built
in a new build directory. Old factory objects cannot qualify the new scheduler.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('native_wave_standalone', HERE / 'run_standalone_contracts.py')
standalone = importlib.util.module_from_spec(spec)
spec.loader.exec_module(standalone)

CMAKE = 'cmake/CudaBackend.cmake'
CMAKE_LF_SHA256 = 'cab12b2447848638d1e15dfe572db74205b9e693aaeb66daa8ddb0a8b0f0585b'
ANCHOR = b'            src/cuda/microphysics/CuDssSparseSolver.cpp\n'
ADDITION = b'            src/cuda/microphysics/CuDssSparseWaveSolver.cpp\n'
ARTIFACT_NAMES = {'CuDssSparseWaveSolver.cpp.o', 'SparseEquilibration.cu.o',
                  'test_sparse_wave.cpp.o', 'test_sparse_wave'}


def sha_bytes(data):
    return hashlib.sha256(data).hexdigest()


def cmake_overlay(raw):
    normalized = raw.replace(b'\r\n', b'\n')
    if sha_bytes(normalized) != CMAKE_LF_SHA256:
        raise ValueError('canonical CMake identity changed; no fuzzy patch')
    if normalized.count(ANCHOR) != 1 or ADDITION in normalized:
        raise ValueError('missing/ambiguous provider owner')
    # Keep the original provider for existing contracts/result handling; only
    # add the execution-only candidate to the same strict-FP, IR=2 owner.
    result = normalized.replace(ANCHOR, ANCHOR + ADDITION, 1)
    if result.replace(ADDITION, b'', 1) != normalized:
        raise AssertionError('CMake edit exceeded the one-source registration')
    return result


def verify_contract_receipt(receipt, payload, shared_manifest):
    receipt = Path(receipt).resolve(strict=True)
    payload = Path(payload).resolve(strict=True)
    shared_manifest = Path(shared_manifest).resolve(strict=True)
    overlay, _ = standalone.verify_payload(payload, shared_manifest)
    record = json.loads(receipt.read_text())
    if (record.get('status') != 'standalone-provider-contracts-passed'
            or record.get('scope') != 'standalone-native-wave-provider-not-ODE-or-ARCH'
            or record.get('release_qualified') is not False
            or record.get('identities_verified_after_run') is not True
            or record.get('payload') != overlay):
        raise ValueError('matching real standalone qualification is required')
    expected_tests = {f'contract-n{n}-c{c}': (n, c) for n, c in standalone.MATRIX}
    if set(record.get('tests', {})) != set(expected_tests):
        raise ValueError('all eight standalone contracts are required')
    commands = record.get('commands', [])
    names = [row.get('name') for row in commands]
    expected_commands = set(expected_tests) | {
        'hardware', 'host-compiler', 'cuda-compiler', 'kernel',
        'compile-provider', 'compile-shared-math', 'compile-test', 'link-test', 'dynamic-libraries'}
    if (len(names) != len(set(names)) or set(names) != expected_commands
            or any(row.get('status') != 'passed' or row.get('returncode') != 0 for row in commands)):
        raise ValueError('successful fresh build and contract command ledger required')
    for name, (extent, capacity) in expected_tests.items():
        test = record['tests'][name]
        if test != dict(passed=True, extent=extent, capacity=capacity):
            raise ValueError('contract dimensions or completion changed')
        marker = f'SPARSE_WAVE_CONTRACT_PASS extent={extent} capacity={capacity} '
        lines = (receipt.parent / (name + '.stdout')).read_text().splitlines()
        if sum(line.startswith(marker) for line in lines) != 1:
            raise ValueError('missing/ambiguous real completion marker: ' + name)
    artifacts = record.get('artifacts', {})
    if {Path(p).name for p in artifacts} != ARTIFACT_NAMES or len(artifacts) != 4:
        raise ValueError('all four fresh standalone artifacts required')
    for path in artifacts:
        original = Path(path)
        if original.is_symlink() or original.resolve(strict=True).parent != receipt.parent:
            raise ValueError('standalone artifact is not owned by this evidence directory')
    inputs = record.get('inputs', {})
    for required in (payload / 'overlay-record.json', shared_manifest):
        if inputs.get(str(required)) != standalone.sha(required):
            raise ValueError('standalone input is not bound to the selected payload')
    for path, expected in {**inputs, **artifacts}.items():
        if standalone.sha(path) != expected:
            raise ValueError('standalone input/product identity changed: ' + path)
    return overlay, record


def prepare(root, payload, receipt, shared_manifest, output):
    root, payload = Path(root).resolve(strict=True), Path(payload).resolve(strict=True)
    receipt = Path(receipt).resolve(strict=True)
    output = Path(output)
    if output.exists() or output.is_symlink():
        raise ValueError('new output required; preserve existing/partial evidence')
    output = output.parent.resolve(strict=True) / output.name
    protected = [payload, receipt.parent, *(root / p for p in ('src', 'cmake', 'tests', '.git'))]
    if root.is_relative_to(output) or any(
            output.is_relative_to(p) or p.is_relative_to(output) for p in protected):
        raise ValueError('output overlaps canonical source or qualification evidence')
    overlay, _ = verify_contract_receipt(receipt, payload, shared_manifest)
    raw_cmake = (root / CMAKE).read_bytes()
    files = {relative: (payload / relative).read_bytes() for relative in overlay['files']}
    for relative, data in files.items():
        if dict(bytes=len(data), sha256=sha_bytes(data)) != overlay['files'][relative]:
            raise ValueError('payload changed while preparing integration: ' + relative)
    files[CMAKE] = cmake_overlay(raw_cmake)
    record = dict(status='preparing', release_qualified=False, pending=None, files={},
        scope='test-only source overlay; factory and ARCH still require fresh compilation and validation',
        standalone_receipt_sha256=standalone.sha(receipt),
        standalone_payload_record_sha256=standalone.sha(payload / 'overlay-record.json'),
        canonical_cmake_raw_sha256=sha_bytes(raw_cmake),
        canonical_cmake_lf_sha256=CMAKE_LF_SHA256,
        recipe_sha256=standalone.sha(Path(__file__)),
        new_source_tree_required=True, new_build_directory_required=True,
        old_factory_objects_qualify_new_scheduler=False)
    output.mkdir()
    def save():
        (output / 'integration-record.json').write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
    try:
        for relative, data in files.items():
            record['pending'] = relative
            save()
            dest = output / relative
            dest.parent.mkdir(parents=True, exist_ok=True)
            with dest.open('xb') as stream:
                stream.write(data)
            record['files'][relative] = dict(bytes=len(data), sha256=sha_bytes(data))
            record['pending'] = None
        # This is an input-bound receipt, NOT a factory build or ODE test pass.
        record['status'] = 'prepared_factory_overlay_not_built_not_ODE_validated'
    except BaseException as error:
        record.update(status='failed_partial_preserved', error=repr(error))
        raise
    finally:
        save()
    return record


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ('root', 'payload', 'receipt', 'shared-manifest', 'output'):
        parser.add_argument('--' + key, type=Path, required=True)
    args = parser.parse_args()
    result = prepare(args.root, args.payload, args.receipt, args.shared_manifest, args.output)
    print(json.dumps(dict(status=result['status'], files=len(result['files']), release_qualified=False)))
