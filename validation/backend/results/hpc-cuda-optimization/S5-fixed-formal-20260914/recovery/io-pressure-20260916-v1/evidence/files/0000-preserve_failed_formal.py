"""Preserve the inspected partial ROS4/RKL1 attempt; never resume/mix samples.

Archive first, verify two local copies externally, then relocate exact unchanged
original entries with a journal. No deletion, process restart, or scientific edit.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

PROJECTS = Path('/home/ubuntu/projects')
ROOT = PROJECTS / 'ARCH-multiphysics-fix-20260914'
BASELINE = PROJECTS / 'ARCH-corrected-fused-baseline-20260914'
BASE = ROOT / 'build/fix-20260914/timing'
MODULE = 'coupled_ros4_rkl1_all_transport'
LABEL = 'formal-' + MODULE + '-v1'
CONTROL = 'controller-v3-' + LABEL
NAME = 'failed-ros4-rkl1-io-pressure-20260916-v1'
OUT = BASE / NAME
RAW = ROOT / 'build' / (NAME + '.tar.zst')
PACK = ROOT / 'build' / (NAME + '-compact.tar.zst')
SUFFIXES = ('-controller-recipe-v3.sh', '-controller-recipe-v3.sha256',
            '-idle-preflight.log', '-wall-policy-v3.json', '-child.log',
            '-guard.log', '-host-before.log', '.stdout', '.stderr')


def require(value, message):
    if not value:
        raise RuntimeError(message)


def sha(path):
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        while block := stream.read(1024 * 1024):
            digest.update(block)
    return digest.hexdigest()


def identity(path):
    return dict(bytes=path.stat().st_size, sha256=sha(path))


def save_new(path, value):
    with path.open('x', encoding='utf-8', newline='\n') as stream:
        stream.write(json.dumps(value, indent=2) + '\n')


def quiescent():
    for name in ('ARCH', 'nvcc', 'ptxas', 'cc1plus'):
        require(subprocess.run(['pgrep', '-x', name], stdout=subprocess.DEVNULL).returncode == 1,
                'Application/build active or check unavailable')
    result = subprocess.run(['nvidia-smi', '--query-compute-apps=pid', '--format=csv,noheader'],
                            capture_output=True, text=True, check=True)
    require(not result.stdout.strip(), 'GPU process active')


def worker_exited(control):
    require({p.name for p in control.iterdir()} == {'pid', 'worker.log', 'lock', 'exit-code'},
            'Unexpected controller entries')
    require(all(p.is_file() and not p.is_symlink() for p in control.iterdir()),
            'Controller entry is linked or nonregular')
    pid = int((control / 'pid').read_text().strip())
    require(pid == 4125711, 'Not the inspected failed worker')
    result = subprocess.run(['ps', '-p', str(pid), '-o', 'stat='], capture_output=True, text=True)
    require((result.returncode == 1 and not result.stdout.strip()) or
            (result.returncode == 0 and result.stdout.strip().startswith('Z')),
            'Original worker active, PID reused, or process check failed')
    return int((control / 'exit-code').read_text().strip())


def validate_failure(report, guard, code):
    require(code == 125, 'Not the inspected resource-guard exit')
    require('MEMORY_GUARD_STOP reason=io_pressure:' in guard and
            'guard_stopped=True' in guard and 'stop_reason=io_pressure' in guard,
            'Missing original I/O pressure stop evidence')
    require(report['status'] == 'running' and not report['pilot'] and
            'identities_after' not in report, 'Not the inspected interrupted report')
    lanes = report['lanes']
    require(len(lanes) == 81 and len(report['comparisons']) == 77,
            'Partial inventory changed; inspect again')
    require(all(v['status'] == 'passed' and v['returncode'] == 0 and not v['timed_out']
                for v in lanes[:-1]), 'An earlier lane failed')
    final = lanes[-1]
    expected = dict(case=MODULE + '_b128', phase='measured', repeat=0,
                    version='candidate', backend='cpu', threads=1, status='running')
    require(all(final[k] == v for k, v in expected.items()), 'Interrupted lane differs')
    require(all(v['fields']['passed'] and v['fields']['status'] == 'pass' and
                v['workload_aligned'] for v in report['comparisons']),
            'Earlier comparison failed; separate diagnosis required')


def select_tops(base):
    require(base.is_dir() and not base.is_symlink(), 'Invalid timing root')
    expected = {LABEL, *(LABEL + s for s in SUFFIXES)}
    actual = {p.name for p in base.iterdir() if p.name.startswith(LABEL)}
    require(actual == expected, 'Unexpected phase-prefix inventory')
    require(not (base / ('collection-' + MODULE + '-v1.log')).exists(),
            'Collection already started; do not move it')
    require((base / LABEL).is_dir(), 'Missing partial run directory')
    for suffix in SUFFIXES:
        require((base / (LABEL + suffix)).is_file(), 'Expected prefix file missing')
    require((base / CONTROL).is_dir(), 'Missing failed controller')
    # The original driver refuses an existing controller directory. Keep that
    # namespace barrier until all original run/prefix entries are preserved.
    return [base / name for name in sorted(expected)] + [base / CONTROL]


def inventory(base, tops):
    rows = {}
    root = base.resolve(strict=True)
    for top in tops:
        require(not top.is_symlink() and top.resolve(strict=True).is_relative_to(root),
                'Top entry escapes timing scope')
        members = list(top.rglob('*')) if top.is_dir() else [top]
        for path in members:
            require(not path.is_symlink() and path.resolve(strict=True).is_relative_to(root),
                    'Linked or escaped member')
            if path.is_dir():
                continue
            require(path.is_file(), 'Nonregular evidence member')
            name = path.relative_to(base).as_posix()
            require(name not in rows, 'Overlapping inventory roots')
            rows[name] = identity(path)
    return dict(sorted(rows.items()))


def capture_failure_identity(report):
    os.environ.update(OMP_NUM_THREADS='8', OMP_DYNAMIC='FALSE', OMP_PLACES='cores', OMP_PROC_BIND='close')
    sys.path.insert(0, str(ROOT / 'tools'))
    import validation_provenance as provenance
    after = {}
    for name, root, build in (
        ('candidate', ROOT, ROOT / 'build/fix-20260914/release'),
        ('baseline', BASELINE, BASELINE / 'build/corrected-fused-20260914/release')):
        after[name] = provenance.capture(source_root=root, build_dir=build,
            arch=build / 'bin/ARCH', checkpoint_validator=build / 'arch_cuda_single_level_validation')
        provenance.require_unchanged(report['identities_before'][name], after[name])
    require(report['recipe'] == provenance.file_identity(Path(report['recipe']['path'])),
            'Timing recipe changed')
    return after


def archive():
    quiescent()
    require(BASE.resolve(strict=True).is_relative_to(ROOT.resolve(strict=True)), 'Wrong timing root')
    require(not OUT.exists() and not RAW.exists() and not PACK.exists(), 'Existing preservation; inspect it')
    tops = select_tops(BASE)
    code = worker_exited(BASE / CONTROL)
    report = json.loads((BASE / LABEL / 'evidence.json').read_text())
    validate_failure(report, (BASE / (LABEL + '-guard.log')).read_text(), code)
    before = inventory(BASE, tops)
    after_identity = capture_failure_identity(report)
    # Keep the exact helpers used for this failure and preservation, separately
    # from the original report. Never fabricate identities_after in that report.
    helpers = [ROOT / 'tools/run_memory_guarded.py', Path(report['recipe']['path']), Path(__file__).resolve()]
    for path in helpers:
        require(path.is_file() and not path.is_symlink() and path.resolve().is_relative_to(PROJECTS),
                'Helper outside projects scope')
    all_files = {str((BASE / k).relative_to(PROJECTS)): v for k, v in before.items()}
    all_files.update({str(p.relative_to(PROJECTS)): identity(p) for p in helpers})
    compact = OUT / 'compact'
    compact.mkdir(parents=True)
    mapping, omitted = {}, {}
    for index, (name, row) in enumerate(sorted(all_files.items())):
        source = PROJECTS / name
        if source.suffix == '.h5' or (source.suffix == '.tsv' and row['bytes'] > 2 * 1024 * 1024):
            omitted[name] = dict(**row, retained_in='complete raw archive')
            continue
        relative = 'evidence.json' if source == BASE / LABEL / 'evidence.json' else f'files/{index:04d}-{source.name}'
        target = compact / relative
        require(not target.exists(), 'Compact projection collision')
        target.parent.mkdir(exist_ok=True)
        shutil.copy2(source, target)
        require(identity(target) == row, 'Projection bytes changed')
        mapping[relative] = name
    save_new(compact / 'raw-manifest.json', all_files)
    save_new(compact / 'projection-map.json', mapping)
    save_new(compact / 'omissions.json', omitted)
    save_new(compact / 'failure-identity.json', after_identity)
    record = dict(status='interrupted_attempt_preserved_not_qualified', module=MODULE,
        completed_runs=80, incomplete_runs=1, comparisons_recorded=77,
        release_qualified=False, scientific_validation_complete=False,
        samples_may_be_mixed_with_retry=False, cause='original system I/O pressure guard',
        controller_exit=code, original_report_unmodified=True,
        source_build_identities_unchanged_after_failure=True, original_top_entries=[p.name for p in tops],
        original_inventory=before, pending_move=None)
    save_new(compact / 'failure-record.json', record)
    paths = OUT / 'raw-paths.nul'
    with paths.open('xb') as stream:
        stream.write(b''.join(name.encode() + b'\0' for name in sorted(all_files)))
    quiescent()
    require(inventory(BASE, tops) == before, 'Failure bytes changed before archive')
    subprocess.run(['tar', '--use-compress-program=zstd -T2 -3', '-cf', str(RAW), '-C', str(PROJECTS),
                    '--null', '-T', str(paths), str(compact.relative_to(PROJECTS))], check=True)
    require(inventory(BASE, tops) == before, 'Failure bytes changed during archive')
    for name, row in all_files.items():
        require(identity(PROJECTS / name) == row, 'Archived helper/source changed')
    raw_record = dict(path=str(RAW), **identity(RAW), files=len(all_files))
    save_new(compact / 'raw-archive.json', raw_record)
    subprocess.run(['tar', '--use-compress-program=zstd -T2 -3', '-cf', str(PACK),
                    '-C', str(OUT), 'compact'], check=True)
    result = dict(status=record['status'], raw=raw_record, compact=dict(path=str(PACK), **identity(PACK)))
    save_new(OUT / 'archive.json', result)
    print(json.dumps(result, indent=2))


def verify_backup(record, raw_sha, raw_bytes, compact_sha):
    require(record['status'] == 'interrupted_attempt_preserved_not_qualified', 'Wrong archive status')
    require(record['raw']['sha256'] == raw_sha and record['raw']['bytes'] == raw_bytes,
            'Local raw attestation differs')
    require(record['compact']['sha256'] == compact_sha, 'Local compact attestation differs')


def move_verified(base, destination, tops, before, backup):
    require(not destination.exists(), 'Preserved destination exists; inspect partial journal')
    require(destination.parent.resolve(strict=True).is_relative_to(base.resolve(strict=True)),
            'Relocation destination escapes scope')
    require(inventory(base, tops) == before, 'Original bytes changed before relocation')
    destination.mkdir()
    journal = dict(status='relocating_failed_attempt', before=before, backup=backup, moved=[], pending_move=None,
                   scientific_validation_complete=False, samples_may_be_mixed_with_retry=False)
    path = destination / 'relocation.json'
    def save():
        temporary = destination / 'relocation.json.tmp'
        save_new(temporary, journal)
        temporary.replace(path)
    save()
    try:
        for source in tops:
            expected = {k: v for k, v in before.items() if k == source.name or k.startswith(source.name + '/')}
            require(inventory(base, [source]) == expected, 'Entry changed before relocation')
            target = destination / source.name
            require(not target.exists(), 'Relocation collision')
            journal['pending_move'] = source.name
            save()
            source.rename(target)
            journal['moved'].append(source.name)
            journal['pending_move'] = None
            save()
        for name, row in before.items():
            require(identity(destination / name) == row, 'Preserved bytes changed')
        journal['status'] = 'failed_attempt_relocated_after_double_backup'
        save()
    except BaseException as error:
        journal.update(status='failed_partial_relocation_preserved', error=repr(error))
        if not (destination / 'relocation.json.tmp').exists():
            save()
        raise
    return journal


def relocate(args):
    quiescent()
    require(not OUT.is_symlink() and OUT.resolve(strict=True).is_relative_to(BASE.resolve(strict=True)),
            'Preservation scope moved or linked')
    record = json.loads((OUT / 'archive.json').read_text())
    verify_backup(record, args.local_raw_sha256, args.local_raw_bytes, args.local_compact_sha256)
    require(record['raw']['path'] == str(RAW) and record['compact']['path'] == str(PACK), 'Wrong archive paths')
    require(identity(RAW) == {k: record['raw'][k] for k in ('bytes', 'sha256')} and
            identity(PACK) == {k: record['compact'][k] for k in ('bytes', 'sha256')}, 'Server backup changed')
    failure = json.loads((OUT / 'compact/failure-record.json').read_text())
    tops = select_tops(BASE)
    code = worker_exited(BASE / CONTROL)
    validate_failure(json.loads((BASE / LABEL / 'evidence.json').read_text()),
                     (BASE / (LABEL + '-guard.log')).read_text(), code)
    quiescent()
    result = move_verified(BASE, OUT / 'preserved', tops, failure['original_inventory'], record)
    print(json.dumps({k: v for k, v in result.items() if k != 'before'}, indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mode', choices=('archive', 'relocate'))
    parser.add_argument('--local-raw-sha256')
    parser.add_argument('--local-raw-bytes', type=int)
    parser.add_argument('--local-compact-sha256')
    options = parser.parse_args()
    archive() if options.mode == 'archive' else relocate(options)
