"""Preserve one failed, never-started formal attempt before a fresh retry.

This is storage/orchestration only. It never restarts a process, deletes data,
changes a sample, or labels a failed preflight as a scientific failure/pass.
Run only at a quiescent barrier, after inspecting the original failure logs.
"""
import hashlib
import json
from pathlib import Path
import subprocess


ROOT = Path('/home/ubuntu/projects/ARCH-multiphysics-fix-20260914')
BASE = ROOT / 'build/fix-20260914/timing'
MODULE = 'coupled_bd_rkl1_all_transport'
LABEL = f'formal-{MODULE}-v1'
CONTROL = f'controller-v3-{LABEL}'
DESTINATION = f'preserved-preflight-{MODULE}-20260915-v1'
SUFFIXES = ('-controller-recipe-v3.sh', '-controller-recipe-v3.sha256',
            '-idle-preflight.log', '-wall-policy-v3.json', '-child.log', '-guard.log')


def require(ok, message):
    if not ok:
        raise RuntimeError(message)


def sha(path):
    h = hashlib.sha256()
    with path.open('rb') as stream:
        while block := stream.read(1024 * 1024):
            h.update(block)
    return h.hexdigest()


def quiescent():
    for name in ('ARCH', 'nvcc', 'ptxas', 'cc1plus'):
        require(subprocess.run(['pgrep', '-x', name], stdout=subprocess.DEVNULL).returncode == 1,
                'Application/build active or process check failed')
    require(subprocess.run(['pgrep', '-f', '^/home/ubuntu/projects/.*/arch_cuda_generated_sparse_burn_'],
                           stdout=subprocess.DEVNULL).returncode == 1,
            'Sparse harness active or process check failed')


def worker_exited(pid):
    result = subprocess.run(['ps', '-p', str(pid), '-o', 'stat='],
                            capture_output=True, text=True)
    require((result.returncode == 1 and not result.stdout.strip()) or
            (result.returncode == 0 and result.stdout.strip().startswith('Z')),
            'Controller still active, PID reused, or process status unavailable')


def inventory(paths):
    result = {}
    for top in paths:
        require(not top.is_symlink() and top.resolve().is_relative_to(BASE.resolve()),
                'Linked or out-of-scope preflight path')
        leaves = list(top.rglob('*')) if top.is_dir() else [top]
        for path in leaves:
            require(not path.is_symlink() and path.resolve().is_relative_to(BASE.resolve()),
                    'Linked or out-of-scope preflight member')
            if path.is_dir():
                continue
            require(path.is_file(), 'Unexpected nonregular preflight member')
            result[path.relative_to(BASE).as_posix()] = {'bytes': path.stat().st_size,
                                                       'sha256': sha(path)}
    return result


def main():
    quiescent()
    require(BASE.is_dir() and not BASE.is_symlink() and
            BASE.resolve().is_relative_to(ROOT.resolve()), 'Wrong timing scope')
    destination = BASE / DESTINATION
    require(not destination.exists(), 'Recovery destination already exists; inspect, do not overwrite')
    control = BASE / CONTROL
    require(control.is_dir() and not control.is_symlink(), 'Expected controller missing or linked')
    require({p.name for p in control.iterdir()} == {'pid', 'worker.log', 'lock', 'exit-code'},
            'Unexpected controller inventory; inspect first')
    for path in control.iterdir():
        require(path.is_file() and not path.is_symlink(), 'Unexpected controller entry')
    exit_code = int((control / 'exit-code').read_text().strip())
    require(exit_code != 0, 'Successful controller must not be reset')
    pid = int((control / 'pid').read_text().strip())
    require(pid > 1, 'Invalid controller PID')
    worker_exited(pid)
    # Presence of any output root or stdout/stderr may mean sampling started.
    # Do not interpret a partial or failed sample as a retryable preflight.
    allowed = {LABEL + suffix for suffix in SUFFIXES}
    actual = set()
    for path in BASE.iterdir():
        if path.name.startswith(LABEL):
            require(path.name in allowed and path.is_file() and not path.is_symlink(),
                    'Sample output or unknown phase artifact exists; preserve in place')
            actual.add(path.name)
    require({LABEL + '-controller-recipe-v3.sh', LABEL + '-controller-recipe-v3.sha256',
             LABEL + '-idle-preflight.log'} <= actual, 'Incomplete preflight recipe evidence')
    require(not (BASE / f'collection-{MODULE}-v1.log').exists(), 'Collection already started')
    paths = [control] + [BASE / name for name in sorted(actual)]
    before = inventory(paths)
    quiescent()
    worker_exited(pid)
    report = {'status': 'preserving', 'module': MODULE, 'controller_pid': pid,
              'controller_exit_code': exit_code, 'sample_output_absent': True,
              'scientific_validation': False, 'source_and_binaries_changed': False,
              'preflight_cause': 'inspect original logs; no automatic causal claim',
              'before': before, 'moved': [], 'pending_move': None}
    destination.mkdir()
    journal = destination / 'preservation.json'

    def save():
        temporary = destination / 'preservation.json.tmp'
        with temporary.open('x', encoding='utf-8', newline='\n') as stream:
            stream.write(json.dumps(report, indent=2) + '\n')
        temporary.replace(journal)

    save()
    try:
        for source in paths:
            require(inventory([source]) == {k: v for k, v in before.items()
                     if k == source.name or k.startswith(source.name + '/')},
                    'Preflight evidence changed before move')
            target = destination / source.name
            require(not target.exists(), 'Preserved destination collision')
            report['pending_move'] = source.name
            save()
            source.rename(target)
            report['moved'].append(source.name)
            report['pending_move'] = None
            save()
        for name, row in before.items():
            path = destination / name
            require(path.is_file() and path.stat().st_size == row['bytes'] and
                    sha(path) == row['sha256'], 'Preserved bytes differ')
        report['status'] = 'preserved_never_started_preflight'
        save()
    except BaseException as error:
        report.update(status='failed_partial_preservation', error=repr(error))
        if not (destination / 'preservation.json.tmp').exists():
            save()
        raise
    print(json.dumps({k: v for k, v in report.items() if k != 'before'}, indent=2))


if __name__ == '__main__':
    main()
