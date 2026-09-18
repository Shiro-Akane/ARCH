"""Compile existing sparse ODE/failure tests against one identified candidate.

The frozen build/source are read-only; compilation, links and logs are separate.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shlex
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--provider', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    args = parser.parse_args()
    build, provider, out = args.build_dir.resolve(), args.provider.resolve(), args.output_dir.resolve()
    if out.exists(): parser.error('refuse to overwrite contract evidence')
    out.mkdir(parents=True)
    entries = json.loads((build / 'compile_commands.json').read_text())
    digest = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
    before = digest(provider)
    record = dict(scope='existing-sparse-ODE-failure-contracts-incremental-build',
                  provider_sha256=before, commands=[], tests={}, status='running')
    def run(name, command, timeout=300):
        row = dict(name=name, command=command, cwd=str(build))
        record['commands'].append(row)
        (out / 'record.json').write_text(json.dumps(record, indent=2) + '\n')
        with (out / f'{name}.stdout').open('w') as stdout, (out / f'{name}.stderr').open('w') as stderr:
            rc = subprocess.run(command, cwd=build, stdout=stdout, stderr=stderr, timeout=timeout).returncode
        row['returncode'] = rc
        if rc != 0: raise RuntimeError(f'{name} failed; logs retained')
    try:
        for target, filename in [('arch_cuda_sparse_be_nr_batch', 'test_sparse_be_nr_batch.cu'),
                                 ('arch_cuda_burn_eos_failure', 'test_burn_eos_failure.cu')]:
            selected = [e for e in entries if e['file'].endswith('/' + filename)]
            if len(selected) != 1: raise RuntimeError('missing/ambiguous original compile recipe')
            entry = selected[0]
            command = shlex.split(entry['command'])
            obj = out / (filename + '.o')
            command[command.index('-o') + 1] = str(obj)
            run('compile-' + target, command)
            recipe = subprocess.run(['ninja', '-t', 'commands', target], cwd=build,
                capture_output=True, text=True, check=True).stdout.splitlines()[-1]
            tokens = shlex.split(recipe)
            if tokens[:2] != [':', '&&'] or tokens[-2:] != ['&&', ':']:
                raise RuntimeError('unrecognized original link recipe')
            command = tokens[2:-2]
            index = [i for i, token in enumerate(command) if token.endswith('/' + filename + '.o')]
            if len(index) != 1 or command.count('libarch_cuda_sparse_provider.a') != 1:
                raise RuntimeError('ambiguous original object/provider binding')
            command[index[0]] = str(obj)
            command[command.index('libarch_cuda_sparse_provider.a')] = str(provider)
            executable = out / target
            command[command.index('-o') + 1] = str(executable)
            run('link-' + target, command)
            run(target, [str(executable)], 120)
            record['tests'][target] = dict(passed=True, sha256=digest(executable),
                                          source_sha256=digest(Path(entry['file'])))
        if digest(provider) != before: raise RuntimeError('candidate changed during contracts')
        record['provider_identity_verified_after_run'] = True
        record['status'] = 'passed'
    except Exception as error:
        record['status'] = 'failed'
        record['error'] = repr(error)
        raise
    finally:
        (out / 'record.json').write_text(json.dumps(record, indent=2) + '\n')
    print(json.dumps(record['tests'], indent=2))


if __name__ == '__main__':
    main()
