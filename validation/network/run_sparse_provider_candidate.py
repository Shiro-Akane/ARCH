"""Build only an ABI-compatible sparse-provider candidate against frozen objects.

This is a focused incremental experiment, not a clean/full build certificate.
Native compile/link recipes are retained; baseline files are never overwritten.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import shlex
import subprocess
import sys


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-root', type=Path, required=True)
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--overlay', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    args = parser.parse_args()
    root, build, overlay, out = (getattr(args, k).resolve()
        for k in ('source_root', 'build_dir', 'overlay', 'output_dir'))
    if out.exists():
        parser.error('refuse to overwrite candidate experiment')
    out.mkdir(parents=True)
    sys.path.insert(0, str(root / 'tools'))
    import validation_provenance as provenance
    targets = ['arch_cuda_cudss_sparse_solver', 'arch_cuda_generated_sparse_burn_audit150',
               'arch_cuda_generated_sparse_burn_audit200']
    artifacts = {name: build / name for name in targets}
    artifacts['provider'] = build / 'libarch_cuda_sparse_provider.a'
    before = provenance.capture_focused(source_root=root, build_dir=build, artifacts=artifacts)
    record = dict(scope='incremental-ABI-compatible-provider-experiment-not-full-build',
                  status='building', release_qualified=False, identity_before=before,
                  overlay_sha256={str(p.relative_to(overlay)): sha(p)
                      for p in overlay.rglob('*') if p.is_file()}, commands=[], tests={})

    def save():
        (out / 'record.json').write_text(json.dumps(record, indent=2, sort_keys=True) + '\n')

    def run(name, command, timeout=300):
        record['commands'].append(dict(name=name, command=command, cwd=str(build)))
        save()
        with (out / f'{name}.stdout').open('w') as stdout, (out / f'{name}.stderr').open('w') as stderr:
            process = subprocess.run(command, cwd=build, stdout=stdout, stderr=stderr, timeout=timeout)
        record['commands'][-1]['returncode'] = process.returncode
        save()
        return process.returncode

    try:
        entries = json.loads((build / 'compile_commands.json').read_text())
        objects = []
        for name in ('CuDssSparseSolver.cpp', 'SparseEquilibration.cu'):
            selected = [e for e in entries if e['file'].endswith('/' + name)
                        and 'arch_cuda_sparse_provider.dir' in e['command']]
            if len(selected) != 1:
                raise RuntimeError('expected one provider compile recipe')
            entry = selected[0]
            command = shlex.split(entry['command'])
            source = overlay / 'src/cuda/microphysics' / name
            command[command.index(entry['file'])] = str(source)
            target = out / (name + '.o')
            command[command.index('-o') + 1] = str(target)
            command[1:1] = ['-I' + str(overlay / 'src'), '-I' + str(root / 'src/cuda/microphysics')]
            if run('compile-' + name, command) != 0:
                raise RuntimeError('provider compile failed; complete command/log retained')
            objects.append(target)
        members = subprocess.run(['ar', 't', str(artifacts['provider'])], capture_output=True,
                                 text=True, check=True).stdout.splitlines()
        if sorted(members) != sorted(p.name for p in objects):
            raise RuntimeError('would omit an original provider archive member')
        archive = out / 'libarch_cuda_sparse_provider.a'
        if run('archive', ['ar', 'rcs', str(archive), *(str(p) for p in objects)]) != 0:
            raise RuntimeError('archive failed')
        # Recompile only the provider contract: its exact engineering traffic
        # counts reflect the extra status integer/kernel, not looser accuracy.
        selected = [e for e in entries if e['file'].endswith('/test_cudss_sparse_solver.cpp')]
        if len(selected) != 1: raise RuntimeError('expected one provider contract recipe')
        entry = selected[0]
        command = shlex.split(entry['command'])
        command[command.index(entry['file'])] = str(overlay / 'tests/cuda/test_cudss_sparse_solver.cpp')
        contract_object = out / 'test_cudss_sparse_solver.cpp.o'
        command[command.index('-o') + 1] = str(contract_object)
        command[1:1] = ['-I' + str(overlay / 'src')]
        if run('compile-provider-contract', command) != 0:
            raise RuntimeError('provider contract compile failed')
        for target in targets:
            recipe = subprocess.run(['ninja', '-t', 'commands', target], cwd=build,
                                    capture_output=True, text=True, check=True).stdout.splitlines()[-1]
            tokens = shlex.split(recipe)
            if tokens[:2] != [':', '&&'] or tokens[-2:] != ['&&', ':']:
                raise RuntimeError('unrecognized native link recipe')
            command = tokens[2:-2]
            command[command.index('-o') + 1] = str(out / target)
            if command.count('libarch_cuda_sparse_provider.a') != 1:
                raise RuntimeError('expected exactly one native provider archive')
            command[command.index('libarch_cuda_sparse_provider.a')] = str(archive)
            if target == targets[0]:
                indices = [i for i, token in enumerate(command)
                           if token.endswith('/test_cudss_sparse_solver.cpp.o')]
                if len(indices) != 1: raise RuntimeError('expected one provider test object')
                command[indices[0]] = str(contract_object)
            if run('link-' + target, command) != 0:
                raise RuntimeError('native target relink failed')
        host_test = out / 'arch_sparse_residual'
        host_command = ['/usr/bin/g++-11', '-std=c++20', '-O3', '-fno-fast-math', '-ffp-contract=off',
                        '-I' + str(overlay / 'src'), '-I' + str(root / 'src'),
                        str(overlay / 'tests/host/test_sparse_residual.cpp'), '-o', str(host_test)]
        if run('compile-host-contract', host_command) != 0:
            raise RuntimeError('shared residual contract compile failed')
        for name, command in [('host-contract', [str(host_test)]),
                              ('provider-contract', [str(out / targets[0])])]:
            rc = run(name, command, 120)
            record['tests'][name] = dict(returncode=rc, passed=rc == 0)
            if rc != 0: raise RuntimeError('contract failed (skip is not pass)')
        record['candidate_sha256'] = {p.name: sha(p) for p in [archive, host_test, *(out / t for t in targets)]}
        spec = importlib.util.spec_from_file_location('native_sparse_validation', root / 'validation/network/run_sparse_validation.py')
        original = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(original)
        for network in ('audit150', 'audit200'):
            command = [str(out / f'arch_cuda_generated_sparse_burn_{network}'), '1e7', '3e9',
                       '1e-10', '1e8', '1e-7', '4', 'c12=0.5', 'o16=0.5']
            rc = run(network, command, 1200)
            item = dict(returncode=rc, focused_gate_pass=False)
            record['tests'][network] = item
            if rc == 0:
                metadata = [r['manifest'] for r in before['build']['registered_networks']
                            if r['manifest']['network_id'] == network]
                if len(metadata) != 1: raise RuntimeError('missing original network identity')
                item['summary'] = original.parse_transcript((out / f'{network}.stdout').read_text(),
                    metadata[0], dict(rho=1e7, temperature=3e9, interval=1e-10, cv=1e8, rtol=1e-7), 4)
                item['focused_gate_pass'] = True
            save()
        provenance.require_unchanged(before,
            provenance.capture_focused(source_root=root, build_dir=build, artifacts=artifacts))
        record['baseline_identity_verified_after_run'] = True
        record['status'] = 'focused-passed' if all(t.get('focused_gate_pass', t.get('passed', False))
                           for t in record['tests'].values()) else 'focused-failed'
        save()
        print(json.dumps(dict(status=record['status'], tests=record['tests']), indent=2))
        return 0 if record['status'] == 'focused-passed' else 1
    except Exception as error:
        record['status'] = 'failed'
        record['error'] = repr(error)
        save()
        raise


if __name__ == '__main__':
    sys.exit(main())
