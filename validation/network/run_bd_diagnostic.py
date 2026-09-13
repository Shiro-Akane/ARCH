"""Compile a Host BD / native-provider shadow diagnostic without rebuilding frozen targets.

This uses the existing generated-network compile flags and existing KLU archives.
It writes separate outputs and never qualifies changed-reference results.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shlex
import subprocess


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--network', choices=('audit150', 'audit200'), required=True)
    args = parser.parse_args()
    build, out = args.build_dir.resolve(), args.output_dir.resolve()
    source = Path(__file__).resolve().with_name('diagnose_sparse_bd.cpp')
    if out.exists():
        parser.error('refuse to overwrite diagnostics')
    out.mkdir(parents=True)
    entries = json.loads((build / 'compile_commands.json').read_text())
    matches = [x for x in entries if x['file'].endswith('/test_generated_sparse_burn.cpp')
               and f'NetCustom_{args.network}' in x['command']]
    if len(matches) != 1:
        raise RuntimeError('expected one registered native compile command')
    entry = matches[0]
    command = shlex.split(entry['command'])
    object_index = command.index('-o')
    del command[object_index:object_index+2]
    command.remove('-c')
    command[command.index(entry['file'])] = str(source)
    executable = out / 'diagnose_sparse_bd'
    # Same already-built external KLU; no change/recompile/download of a library.
    libraries = [build / f'_deps/suitesparse-build/{folder}/{name}' for folder, name in (
        ('KLU', 'libklu.a'), ('AMD', 'libamd.a'), ('COLAMD', 'libcolamd.a'),
        ('SuiteSparse_config', 'libsuitesparseconfig.a'), ('BTF', 'libbtf.a'))]
    if not all(path.is_file() for path in libraries):
        raise RuntimeError('existing KLU archives missing')
    # Reuse the native link recipe (not a Ninja build), replacing only its two
    # test objects. No new dependency paths or ABI guesses are introduced.
    target = f'arch_cuda_generated_sparse_burn_{args.network}'
    native = subprocess.run(['ninja', '-t', 'commands', target], cwd=build,
                            capture_output=True, text=True, check=True).stdout.splitlines()[-1]
    link_tokens = shlex.split(native)
    begin = link_tokens.index('-o') + 2
    link_tail = link_tokens[begin:]
    if '&&' in link_tail:
        link_tail = link_tail[:link_tail.index('&&')]
    command += ['-o', str(executable), *link_tail]
    identity_paths = [source, Path(__file__).resolve(), build / 'compile_commands.json',
                      build / 'CMakeCache.txt', *libraries,
                      build / f'arch_cuda_generated_sparse_burn_{args.network}',
                      build / 'libarch_cuda_sparse_provider.a']
    before = {str(path): digest(path) for path in identity_paths}
    record = dict(scope='test-only-shadow-BD-not-qualification', command=command,
                  original_compile_entry=entry, original_link_command=native,
                  identity_before=before, runs=[])
    (out / 'record.json').write_text(json.dumps(record, indent=2) + '\n')
    with (out / 'compile.log').open('w') as log:
        subprocess.run(command, cwd=entry['directory'], stdout=log, stderr=subprocess.STDOUT,
                       check=True, timeout=300)
    record['diagnostic_executable_sha256'] = digest(executable)
    for mode in ('observe', 'fresh', 'refine', 'cudss', 'cudss_refine'):
        invocation = [str(executable), mode]
        result = subprocess.run(invocation, capture_output=True, text=True, timeout=120)
        (out / f'{mode}.stdout').write_text(result.stdout)
        (out / f'{mode}.stderr').write_text(result.stderr)
        record['runs'].append(dict(command=invocation, returncode=result.returncode))
        print(result.stdout, end='')
        print(result.stderr, end='')
        result.check_returncode()
    after = {str(path): digest(path) for path in identity_paths}
    if after != before:
        raise RuntimeError('diagnostic inputs / frozen binary changed')
    record['identity_verified_after_run'] = True
    record['status'] = 'diagnostic-completed-not-qualification'
    (out / 'record.json').write_text(json.dumps(record, indent=2) + '\n')


if __name__ == '__main__':
    main()
