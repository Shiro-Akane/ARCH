"""Collect a finished window factory build; never builds or runs physics.

Success qualifies only two fresh factory executables. All attempts, including
partial compilation failures, retain original commands, inputs and products.
"""
import hashlib
import importlib.util
import json
import math
from pathlib import Path, PurePosixPath
import re
import shlex
import shutil
import subprocess
import sys

ROOT = Path('/home/ubuntu/projects/ARCH-native-wave-v4-20260916')
HERE = Path(__file__).resolve().parent
# Give the shared collector a distinct name: this file deliberately has the
# same basename but must never import itself as the parent archive helper.
parent = ROOT/'input/collect_factory.py' if (ROOT/'input').is_dir() else HERE.parent/'collect_factory.py'
spec = importlib.util.spec_from_file_location('window_parent_archive_helpers', parent)
archive = importlib.util.module_from_spec(spec)
spec.loader.exec_module(archive)
sys.path.insert(0, str(ROOT/'window-factory-input-v1' if (ROOT/'window-factory-input-v1').is_dir() else HERE))
from factory_recipe import compile_recipe, dependency_recipe, link_recipe, source_plan, verify_factory_dependencies
from build_factory import require_backup


def configured_ninja(cache_text):
    matches = re.findall(r'^CMAKE_MAKE_PROGRAM:FILEPATH=(.+)$', cache_text, re.MULTILINE)
    if len(matches) != 1:
        raise ValueError('one frozen CMake build tool required')
    value = matches[0].strip()
    if not PurePosixPath(value).is_absolute() or PurePosixPath(value).name != 'ninja':
        raise ValueError('frozen absolute Ninja path required')
    return value


def canonical_archive_paths(paths, root):
    root = root.resolve(strict=True)
    if any(p.is_symlink() or not p.is_file() or not p.resolve(strict=True).is_relative_to(root) for p in paths):
        raise ValueError('unsafe factory archive input')
    # NVCC records legitimate ../ include aliases. Keep those strings in the
    # build record, but archive each canonical regular file exactly once.
    return {p.resolve(strict=True) for p in paths}


def validate_transcript(record, expected):
    if (record.get('status') != 'factory-built-not-runtime-qualified'
            or record.get('identities_verified_after') is not True
            or record.get('fresh_cuda_objects') is not True
            or record.get('window_contracts_passed') is not True
            or any(record.get(name) is not False for name in
                   ('original_provider_modified', 'mathematical_bodies_modified', 'kernel_launch_shape_modified',
                    'production_registration_modified', 'nuclear_qualified', 'performance_qualified', 'release_qualified'))):
        raise ValueError('completed fresh build with unexpanded scope required')
    commands = record.get('commands', [])
    if len(expected) != 11 or len(commands) != len(expected):
        raise ValueError('all eleven actual build/dependency commands required')
    for actual, wanted in zip(commands, expected):
        if (any(actual.get(key) != value for key, value in wanted.items())
                or actual.get('status') != 'passed' or actual.get('returncode') != 0
                or actual.get('timed_out') is not False
                or not isinstance(actual.get('elapsed_seconds'), (int, float))
                or not math.isfinite(actual['elapsed_seconds']) or actual['elapsed_seconds'] < 0):
            raise ValueError('actual build command failed, reordered or differed')


def validate(record, output):
    source, build, private = ROOT/'source', ROOT/'factory-release', output/'source'
    payload = ROOT/'window-factory-input-v1'
    window = ROOT/'window-input-v1'
    parents = require_backup(ROOT)
    cache = build/'CMakeCache.txt'
    if record.get('inputs', {}).get(str(cache)) != archive.sha(cache):
        raise ValueError('frozen CMake cache changed before recipe audit')
    ninja = configured_ninja(cache.read_text())
    if not Path(ninja).is_file():
        raise ValueError('original configured Ninja is unavailable')
    entries = json.loads((build/'compile_commands.json').read_text())
    provider = ROOT/'batch-launch-contract-v1/libarch_cuda_sparse_provider.a'
    expected = []

    def add(name, command, cwd=build, timeout=300):
        expected.append(dict(name=name, command=command, cwd=str(cwd), timeout_seconds=timeout))

    wrapper = output/'window.o'
    recipe = compile_recipe(entries, 150, 'wrapper', source, private, wrapper, output/'window.d')
    add('compile-wrapper', recipe['command'], Path(recipe['cwd']))
    products = [wrapper]
    for network in (150, 200):
        factory, host = output/f'audit{network}-factory.o', output/f'audit{network}-host.o'
        for kind, obj, timeout in (('factory', factory, 12000), ('harness', host, 300)):
            dep = output/f'audit{network}-{kind}.d'
            recipe = compile_recipe(entries, network, kind, source, private, obj, dep)
            if kind == 'factory':
                preflight = output/f'audit{network}-preflight.d'
                add(f'dependencies-audit{network}', dependency_recipe(recipe['command'], preflight), Path(recipe['cwd']))
                verify_factory_dependencies(preflight.read_text(), obj, source, private)
                names = verify_factory_dependencies(dep.read_text(), obj, source, private)
                actual = {name: archive.sha(Path(name)) for name in names}
                if record.get('dependencies', {}).get(str(network)) != actual:
                    raise ValueError('factory dependency identity differs from actual compiler output')
            add(f'compile-audit{network}-{kind}', recipe['command'], Path(recipe['cwd']), timeout)
            products.append(obj)
        target = f'arch_cuda_generated_sparse_burn_audit{network}'
        native = shlex.split(subprocess.run([ninja, '-t', 'commands', target], cwd=build,
            capture_output=True, text=True, check=True).stdout.splitlines()[-1])
        exe = output/target
        add(f'link-audit{network}', link_recipe(native, host, factory, wrapper, provider, exe))
        add(f'ldd-audit{network}', ['ldd', str(exe)], timeout=30)
        products.append(exe)
    validate_transcript(record, expected)
    if set(record.get('dependencies', {})) != {'150', '200'}:
        raise ValueError('missing or additional factory dependency group')
    artifacts = {str(path): archive.sha(path) for path in products}
    if record.get('artifacts') != artifacts:
        raise ValueError('seven fresh object/executable identities required')
    manifest = ROOT/'factory-control-v2/source-files.sha256'
    originals, content = source_plan(source, manifest.read_bytes(), window)
    expected_private = {name: hashlib.sha256(data).hexdigest() for name, data in content.items()}
    paths = list(private.rglob('*'))
    if any(path.is_symlink() for path in paths):
        raise ValueError('private source cannot contain symlinks')
    actual_private = {p.relative_to(private).as_posix(): archive.sha(p) for p in paths if p.is_file()}
    if (record.get('source_copy') != dict(originals=originals, private=expected_private)
            or actual_private != expected_private):
        raise ValueError('private source is not the reviewed complete copy')
    inputs = [*parents, window/'protocol.py', ROOT/'window-contract-v1/record.json', manifest,
              provider, build/'compile_commands.json', build/'CMakeCache.txt']
    inputs += [path for path in payload.iterdir() if path.is_file()]
    inputs += [window/'CuDssSparseWindowSolver.h', window/'CuDssSparseWindowSolver.cpp']
    if record.get('inputs') != {str(path): archive.sha(path) for path in inputs}:
        raise ValueError('fresh build inputs changed or omitted')
    return dict(factory_build_pass=True, fresh_factories=2, nuclear_qualified=False,
                performance_qualified=False, release_qualified=False)


def main():
    control, output = ROOT/'window-factory-control-v1', ROOT/'window-factory-v1'
    code = archive.completion(control)
    for name in ('ARCH', 'nvcc', 'ptxas', 'cc1plus', 'window-test',
                 'arch_cuda_generated_sparse_burn_audit150', 'arch_cuda_generated_sparse_burn_audit200'):
        if subprocess.run(['pgrep', '-f', '^.*/'+re.escape(name)+r'( |$)'], capture_output=True).returncode != 1:
            raise ValueError('compute remains active or process check failed')
    if subprocess.run(['nvidia-smi', '--query-compute-apps=pid', '--format=csv,noheader'],
                      capture_output=True, text=True, check=True).stdout.strip():
        raise ValueError('GPU remains active; collection must wait')
    record_path = output/'record.json'
    record = json.loads(record_path.read_text()) if record_path.is_file() else {}
    result = dict(worker_exit_code=code, factory_build_pass=False, nuclear_qualified=False,
                  performance_qualified=False, release_qualified=False)
    if code == 0:
        result.update(validate(record, output))
        archive.successful_guard(control/'window-factory-guard.log')
    paths = {path for folder in (control, output, ROOT/'window-factory-input-v1')
             for path in folder.rglob('*') if path.is_file()}
    external = {}
    identity_groups = [record.get('inputs', {}), record.get('artifacts', {}), *record.get('dependencies', {}).values()]
    for group in identity_groups:
        for name, expected in group.items():
            path = Path(name)
            if archive.sha(path) != expected:
                raise ValueError('recorded input/product/dependency changed: '+name)
            if path.resolve().is_relative_to(ROOT):
                paths.add(path)
            else:
                external[name] = dict(resolved=str(path.resolve()), bytes=path.stat().st_size, sha256=expected)
    original = ROOT/'factory-control-v2'
    for name in ('source-files', 'network-files', 'vendor', 'artifacts'):
        manifest = original/(name+'.sha256')
        items = archive.inventory(manifest, ROOT/'source')
        paths.add(manifest)
        if name in ('network-files', 'vendor'):
            for path, expected in items.items():
                external[str(path)] = dict(resolved=str(path.resolve()), bytes=path.stat().st_size, sha256=expected)
    archive.inventory(control/'recipes.sha256', ROOT/'source')
    paths.update(ROOT/name for name in ('window-collection-v1.json', 'window-local-receipt-v1.json',
                                        'factory-focused-collection-v1.json'))
    paths.update((ROOT/'input/collect_factory.py', Path(__file__).resolve()))
    for name in ('collect_window_factory_v1.py', 'collect_window_factory_v2.py',
                 'window-factory-collection-recovery-v1.md', 'window-factory-collection-recovery-v2.md'):
        previous = ROOT/'input'/name
        if previous.is_file():
            paths.add(previous)
    # Preserve the rejected alias-containing archives as opaque evidence, never
    # as an accepted predecessor or inputs to a new scientific execution.
    paths.update(ROOT/name for name in ('window-factory-raw-v1.tar.zst',
        'window-factory-compact-v1.tar.zst', 'window-factory-collection-v1.json'))
    paths = canonical_archive_paths(paths, ROOT)
    evidence = ROOT/'window-factory-evidence-v2'
    raw, packed = ROOT/'window-factory-raw-v2.tar.zst', ROOT/'window-factory-compact-v2.tar.zst'
    receipt = ROOT/'window-factory-collection-v2.json'
    if any(path.exists() for path in (evidence, raw, packed, receipt)):
        raise ValueError('preserve existing/partial factory archive')
    compact = evidence/'compact'
    compact.mkdir(parents=True)
    manifest = {path.relative_to(ROOT.parent).as_posix(): dict(bytes=path.stat().st_size, sha256=archive.sha(path))
                for path in sorted(paths)}
    omitted = []
    private = output/'source'
    changed = {'src/cuda/microphysics/SparseOdeBatch.cuh', 'src/cuda/runtime/burn/CudaBackendBurnSparseImpl.cuh',
               'src/cuda/microphysics/CuDssSparseWindowSolver.h', 'src/cuda/microphysics/CuDssSparseWindowSolver.cpp'}
    for path in sorted(paths):
        relative = path.relative_to(ROOT.parent)
        with path.open('rb') as stream:
            binary = stream.read(8).startswith((b'\x7fELF', b'!<arch>'))
        unchanged_source = path.is_relative_to(private) and path.relative_to(private).as_posix() not in changed
        if binary or path.suffix in ('.o', '.a', '.so', '.zst') or unchanged_source:
            omitted.append(relative.as_posix())
            continue
        target = compact/'records'/relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(path, target)
    for name, value in (('raw-manifest.json', manifest), ('raw-only-files.json', omitted),
                        ('external-dependencies.json', external)):
        (compact/name).write_text(json.dumps(value, indent=2)+'\n')
    subprocess.run(['tar', '--zstd', '-cf', str(raw), '-C', str(ROOT.parent),
                    *manifest, str(compact.relative_to(ROOT.parent))], check=True)
    for path in paths:
        if archive.sha(path) != manifest[path.relative_to(ROOT.parent).as_posix()]['sha256']:
            raise ValueError('factory evidence changed during collection')
    raw_record = dict(path=str(raw), bytes=raw.stat().st_size, sha256=archive.sha(raw), files=len(manifest))
    (compact/'raw-archive.json').write_text(json.dumps(dict(**raw_record, **result), indent=2)+'\n')
    subprocess.run(['tar', '--zstd', '-cf', str(packed), '-C', str(evidence), 'compact'], check=True)
    result.update(raw=raw_record, compact=dict(path=str(packed), bytes=packed.stat().st_size, sha256=archive.sha(packed)),
                  embedded_compact=compact.relative_to(ROOT.parent).as_posix())
    with receipt.open('x') as stream:
        json.dump(result, stream, indent=2)
        stream.write('\n')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
