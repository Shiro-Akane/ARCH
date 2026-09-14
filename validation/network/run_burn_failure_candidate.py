"""Compile the real EOS failure witness with a source overlay and native provider.

Reuses the frozen build's exact compile/link options. This is a focused kernel
gate, not a full ARCH/cuDSS integration or performance certificate.
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
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--build-dir', type=Path, required=True)
    p.add_argument('--overlay', type=Path, required=True)
    p.add_argument('--provider', type=Path, required=True)
    p.add_argument('--output-dir', type=Path, required=True)
    a = p.parse_args()
    build, overlay, provider, out = [v.resolve() for v in
        (a.build_dir, a.overlay, a.provider, a.output_dir)]
    out.mkdir(parents=True, exist_ok=False)
    target = 'arch_cuda_burn_eos_failure'
    def inputs():
        paths = [provider, build/'compile_commands.json', Path(__file__),
                 overlay/'tests/cuda/test_burn_eos_failure.cu',
                 overlay/'tests/fixtures/SparseTransferNetwork.h']
        paths += sorted(p for p in (overlay/'src').rglob('*') if p.is_file())
        return {str(p): digest(p) for p in paths}
    before = inputs()
    report = dict(status='running', scope='focused-kernel-gate-not-full-ARCH',
                  release_qualified=False, inputs_before=before, commands=[])
    def save():
        (out/'record.json').write_text(json.dumps(report, indent=2)+'\n')
    def run(name, command, timeout):
        row = dict(name=name, command=command, cwd=str(build))
        report['commands'].append(row)
        save()
        with (out/(name+'.stdout')).open('w') as stdout, (out/(name+'.stderr')).open('w') as stderr:
            rc = subprocess.run(command, cwd=build, stdout=stdout, stderr=stderr, timeout=timeout).returncode
        row['returncode'] = rc
        save()
        if rc:
            raise RuntimeError(f'{name} failed/was skipped, exit={rc}; original logs retained')
    save()
    try:
        entries = json.loads((build/'compile_commands.json').read_text())
        entries = [e for e in entries if e['file'].endswith('/test_burn_eos_failure.cu')]
        if len(entries) != 1:
            raise RuntimeError('expected exactly one native EOS failure compile recipe')
        entry = entries[0]
        command = shlex.split(entry['command'])
        command[1:1] = ['-I'+str(overlay/'src')]
        command[command.index(entry['file'])] = str(overlay/'tests/cuda/test_burn_eos_failure.cu')
        obj = out/'test_burn_eos_failure.cu.o'
        command[command.index('-o')+1] = str(obj)
        run('compile', command, 1200)
        native = subprocess.run(['ninja','-t','commands',target], cwd=build,
            capture_output=True,text=True,check=True).stdout.splitlines()[-1]
        report['native_link_recipe'] = native
        command = shlex.split(native)
        if command[:2] != [':','&&'] or command[-2:] != ['&&',':']:
            raise RuntimeError('unrecognized native link recipe')
        command = command[2:-2]
        objects = [i for i,v in enumerate(command) if v.endswith('/test_burn_eos_failure.cu.o')]
        if len(objects) != 1 or command.count('libarch_cuda_sparse_provider.a') != 1:
            raise RuntimeError('ambiguous test object or sparse provider')
        command[objects[0]] = str(obj)
        command[command.index('libarch_cuda_sparse_provider.a')] = str(provider)
        exe = out/target
        command[command.index('-o')+1] = str(exe)
        run('link', command, 300)
        report['executable_sha256'] = digest(exe)
        run('test', [str(exe)], 600)
        text = (out/'test.stdout').read_text()
        for table in ('Tabular3','Tabular4'):
            if text.count(table+': dense/sparse/batched EOS failure no-commit and valid reuse passed') != 3:
                raise RuntimeError('missing one of the six actual EOS/ODE failure gates')
        report['status'] = 'passed'
    except BaseException as error:
        report.update(status='failed',error=repr(error))
        raise
    finally:
        after = inputs()
        report['input_identity_verified_after_run'] = before == after
        if before != after:
            report.update(status='failed',identity_error='overlay/provider/native recipe changed')
        save()
        if before != after:
            raise RuntimeError(report['identity_error'])
    print('BURN_EOS_FAILURE_CANDIDATE_PASS',flush=True)


if __name__ == '__main__': main()
