"""Build isolated, fresh CUDA window factories only AFTER qualified contracts.

Preparation-stage runner: no dispatcher, runtime matrix, source registration or
performance approval. The original source/build/network/provider remain intact.
"""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shlex
import subprocess
import time

from factory_recipe import compile_recipe, dependency_recipe, link_recipe, materialize_source, verify_factory_dependencies

ROOT = Path('/home/ubuntu/projects/ARCH-native-wave-v4-20260916')
COMMANDS_SHA = '6a17407cf53fba1ad868e783245ea21c56274db29829e4eea0dd32bc0ba62049'
PROVIDER_SHA = '0b902a1e7d87174bc395d4be328713390d36a28320353d55113678e57f697204'
PROTOCOL_SHA = '808ffb3eb39783993bdd0906c6b65fe697173dfe814ab1538c27c6e685e6314c'


def sha(path):
    digest = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for data in iter(lambda: stream.read(1048576), b''):
            digest.update(data)
    return digest.hexdigest()


def require_backup(root):
    collection_path = root/'window-collection-v1.json'
    receipt_path = root/'window-local-receipt-v1.json'
    collection, receipt = json.loads(collection_path.read_text()), json.loads(receipt_path.read_text())
    if (collection.get('contracts_pass') is not True or collection.get('worker_exit_code') != 0
            or collection.get('contract_count') != 18
            or receipt.get('status') != 'both_archives_and_all_members_byte_verified'
            or any(collection.get(field) is not False for field in
                   ('nuclear_qualified', 'performance_qualified', 'release_qualified'))):
        raise ValueError('completed standalone window contracts and bounded scope required')
    for kind in ('raw', 'compact'):
        item = collection[kind]
        path = Path(item['path'])
        if (receipt.get(kind) != item or path.is_symlink() or not path.is_file()
                or path.stat().st_size != item['bytes'] or sha(path) != item['sha256']):
            raise ValueError('window archive/local receipt is missing or changed')
    return collection_path, receipt_path


def main():
    payload = Path(__file__).resolve().parent
    output, source, build = ROOT/'window-factory-v1', ROOT/'source', ROOT/'factory-release'
    if payload != ROOT/'window-factory-input-v1' or output.exists() or output.is_symlink():
        raise ValueError('expected isolated payload and new output required')
    for name in ('LD_PRELOAD', 'ARCH_SPARSE_ADVANCE_THREADS', 'ARCH_NATIVE_WAVE_WORK_OBSERVER',
                 'ARCH_NATIVE_WINDOW_CELLS'):
        if os.environ.get(name):
            raise ValueError('no inherited experimental override permitted')
    for name in ('ARCH', 'nvcc', 'ptxas', 'cc1plus', 'window-test'):
        if subprocess.run(['pgrep', '-x', name], capture_output=True).returncode != 1:
            raise ValueError('another application or build remains active')
    if subprocess.run(['nvidia-smi', '--query-compute-apps=pid', '--format=csv,noheader'],
                      capture_output=True, text=True, check=True).stdout.strip():
        raise ValueError('GPU must be idle before isolated build')
    parents = require_backup(ROOT)
    contract_output, window = ROOT/'window-contract-v1', ROOT/'window-input-v1'
    protocol_path = window/'protocol.py'
    if sha(protocol_path) != PROTOCOL_SHA:
        raise ValueError('standalone window protocol changed')
    spec = importlib.util.spec_from_file_location('qualified_window_protocol', protocol_path)
    protocol = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(protocol)
    contract = json.loads((contract_output/'record.json').read_text())
    protocol.verify_record(contract, contract_output)
    for path, expected in {**contract['inputs'], **contract['artifacts']}.items():
        if sha(path) != expected:
            raise ValueError('qualified standalone input/product changed: '+path)
    provider = ROOT/'batch-launch-contract-v1/libarch_cuda_sparse_provider.a'
    if sha(provider) != PROVIDER_SHA or sha(build/'compile_commands.json') != COMMANDS_SHA:
        raise ValueError('qualified native provider or original compile recipe changed')
    source_manifest = ROOT/'factory-control-v2/source-files.sha256'
    paths = [*parents, protocol_path, contract_output/'record.json', source_manifest,
             provider, build/'compile_commands.json', build/'CMakeCache.txt']
    paths += [path for path in payload.iterdir() if path.is_file()]
    paths += [window/'CuDssSparseWindowSolver.h', window/'CuDssSparseWindowSolver.cpp']
    if any(path.is_symlink() for path in paths):
        raise ValueError('symlink factory input not permitted')
    record = dict(status='preparing', inputs={str(path): sha(path) for path in paths},
                  source_copy={}, commands=[], artifacts={}, dependencies={},
                  fresh_cuda_objects=True, window_contracts_passed=True,
                  original_provider_modified=False, mathematical_bodies_modified=False,
                  kernel_launch_shape_modified=False, production_registration_modified=False,
                  nuclear_qualified=False, performance_qualified=False, release_qualified=False)
    output.mkdir()

    def save():
        (output/'record.json').write_text(json.dumps(record, indent=2)+'\n')

    def run(name, command, cwd=build, timeout=300):
        item = dict(name=name, command=command, cwd=str(cwd), timeout_seconds=timeout,
                    status='running', timed_out=False)
        record['commands'].append(item)
        save()
        print('WINDOW_FACTORY_STARTED '+name, flush=True)
        start = time.monotonic()
        try:
            with (output/(name+'.stdout')).open('wb') as stdout, (output/(name+'.stderr')).open('wb') as stderr:
                result = subprocess.run(['/usr/bin/time', '-v', '--', *command], cwd=cwd,
                                        stdout=stdout, stderr=stderr, timeout=timeout)
            item.update(returncode=result.returncode, status='passed' if result.returncode == 0 else 'failed')
            if result.returncode:
                raise RuntimeError('first fresh factory failure retained: '+name)
        except subprocess.TimeoutExpired:
            item.update(returncode=None, status='failed', timed_out=True)
            raise
        except BaseException as error:
            item.update(status='failed', error=repr(error))
            raise
        finally:
            item['elapsed_seconds'] = time.monotonic()-start
            save()
        print('WINDOW_FACTORY_FINISHED '+name, flush=True)

    try:
        private = output/'source'
        record['source_copy'] = materialize_source(source, source_manifest.read_bytes(), window, private)
        entries = json.loads((build/'compile_commands.json').read_text())
        wrapper = output/'window.o'
        recipe = compile_recipe(entries, 150, 'wrapper', source, private, wrapper, output/'window.d')
        run('compile-wrapper', recipe['command'], Path(recipe['cwd']))
        record['artifacts'][str(wrapper)] = sha(wrapper)
        for network in (150, 200):
            factory, host = output/f'audit{network}-factory.o', output/f'audit{network}-host.o'
            for kind, obj, timeout in (('factory', factory, 12000), ('harness', host, 300)):
                dependency = output/f'audit{network}-{kind}.d'
                recipe = compile_recipe(entries, network, kind, source, private, obj, dependency)
                if kind == 'factory':
                    preflight = output/f'audit{network}-preflight.d'
                    run(f'dependencies-audit{network}', dependency_recipe(recipe['command'], preflight),
                        Path(recipe['cwd']), 300)
                    verify_factory_dependencies(preflight.read_text(), obj, source, private)
                run(f'compile-audit{network}-{kind}', recipe['command'], Path(recipe['cwd']), timeout)
                record['artifacts'][str(obj)] = sha(obj)
                if kind == 'factory':
                    names = verify_factory_dependencies(dependency.read_text(), obj, source, private)
                    record['dependencies'][str(network)] = {name: sha(name) for name in names}
                save()
            target = f'arch_cuda_generated_sparse_burn_audit{network}'
            original = shlex.split(subprocess.run(['ninja', '-t', 'commands', target], cwd=build,
                capture_output=True, text=True, check=True).stdout.splitlines()[-1])
            exe = output/target
            run(f'link-audit{network}', link_recipe(original, host, factory, wrapper, provider, exe))
            record['artifacts'][str(exe)] = sha(exe)
            run(f'ldd-audit{network}', ['ldd', str(exe)], timeout=30)
        current = {path.relative_to(private).as_posix(): sha(path) for path in private.rglob('*') if path.is_file()}
        if current != record['source_copy']['private']:
            raise ValueError('private source inventory changed during build')
        identities = {**record['inputs'], **record['artifacts']}
        for files in record['dependencies'].values():
            identities.update(files)
        for path, expected in identities.items():
            if sha(path) != expected:
                raise ValueError('factory dependency/product changed: '+path)
        if len(record['commands']) != 11 or len(record['artifacts']) != 7 or len(record['dependencies']) != 2:
            raise ValueError('incomplete fresh factory build')
        record.update(status='factory-built-not-runtime-qualified', identities_verified_after=True)
    except BaseException as error:
        record.update(status='failed_partial_preserved', error=repr(error))
        raise
    finally:
        save()
    print('WINDOW_FACTORY_BUILD_COMPLETE_RUNTIME_UNQUALIFIED', flush=True)


if __name__ == '__main__':
    main()
