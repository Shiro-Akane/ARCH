"""Preserve the original-physics extended-wall BE follow-up, including failure.

Run only after the follow-up and all application/build processes have exited.
An archive PASS is a storage result, never a scientific/performance PASS.
"""
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

ROOT = Path('/home/ubuntu/projects/ARCH-microphysics-20260914')
PROJECTS = ROOT.parent
BASE = ROOT / 'build/p12-20260914'
RECIPES = BASE / 'be-extended-wall-recipes-v1'
PHASE = 'long-be-extended-wall-v1'
RUN = BASE / 'factor-cache' / PHASE
OUT = BASE / 'factor-cache' / (PHASE + '-archive')
PROVIDER = BASE / 'factor-cache/candidate-v2/libarch_cuda_sparse_provider.a'
OLD_BUILD = PROJECTS / 'ARCH-perf-20260909/build/large-network-20260909/release'


def sha(path):
    h = hashlib.sha256()
    with path.open('rb') as stream:
        while chunk := stream.read(1024 * 1024):
            h.update(chunk)
    return h.hexdigest()


def main():
    for process in ('ARCH', 'nvcc', 'ptxas', 'cc1plus'):
        if subprocess.run(['pgrep', '-x', process], stdout=subprocess.DEVNULL).returncode == 0:
            raise RuntimeError('Application/build still active; do not archive concurrently')
    if subprocess.run(['pgrep', '-f', '^/home/ubuntu/projects/.*/arch_cuda_generated_sparse_burn_'],
                      stdout=subprocess.DEVNULL).returncode == 0:
        raise RuntimeError('Sparse harness still active')
    record = json.loads((RUN / 'record.json').read_text())
    if record['status'] not in ('passed', 'failed'):
        raise RuntimeError('No terminal record; preserve files and investigate first')
    if (record['duration'], record['steps'], record['runtime_timeout_seconds'],
            record['build_command_timeout_seconds']) != (1e-9, 16, 21600, 1800):
        raise RuntimeError('Unexpected follow-up protocol')
    if 'observer' in record:
        raise RuntimeError('This follow-up must not use the observer')

    expected = {f'{network}-be_nr-pool{pool}' for network in ('audit150', 'audit200') for pool in (8, 32)}
    completed = [row['name'] for row in record['runs'] if row['passed']]
    if len(completed) != len(set(completed)) or not set(completed) <= expected:
        raise RuntimeError('Unexpected or duplicate trajectory completion')
    if record['status'] == 'passed' and set(completed) != expected:
        raise RuntimeError('A passed record lacks the complete four-harness matrix')
    expected_steps = [(cells, step) for cells in (32, 33) for step in range(16)]
    for name in completed:
        lines = (RUN / (name + '.stdout')).read_text().splitlines()
        for prefix in ('cpu_step,', 'gpu_step,'):
            sequence = [(int(line.split(',')[2]), int(line.split(',')[3]))
                        for line in lines if line.startswith(prefix)]
            if sequence != expected_steps:
                raise RuntimeError(f'{name}: incomplete or reordered {prefix} steps')
        if lines.count('GENERATED_SPARSE_BURN_PARITY_PASS') != 1:
            raise RuntimeError(f'{name}: missing unique parity completion')

    identities = {
        RECIPES / 'validation/network/run_sparse_capacity.py': record['recipe_sha256'],
        ROOT / 'tests/cuda/test_generated_sparse_burn.cpp': record['source_sha256'],
        PROVIDER: record['provider_sha256'],
        OLD_BUILD / 'compile_commands.json': record['compile_commands_sha256'],
    }
    for artifact in record['artifacts']:
        identities[Path(artifact['factory_path'])] = artifact['factory_sha256']
        if 'executable_path' in artifact:
            identities[Path(artifact['executable_path'])] = artifact['executable_sha256']
    for path, digest in identities.items():
        if not path.resolve().is_relative_to(PROJECTS) or sha(path) != digest:
            raise RuntimeError(f'Identity changed: {path}')
    files = {f for f in RUN.rglob('*') if f.is_file()}
    files.update(identities)
    files.update(f for f in (BASE / 'factor-cache').glob(PHASE + '-*.log')
                 if f.name != PHASE + '-collection.log')
    files.update(RECIPES / name for name in (
        'replay-large-be-extended-wall-20260914.sh',
        'large-be-extended-wall-protocol-20260914.json',
        'archive-large-be-extended-wall-20260914.py',
        'sparse-capacity-wall-budget-tests-20260914.log',
        'sparse-capacity-wall-budget-tests-v2-20260914.log',
        'server-recipe-tests.log',
        'run-large-be-followup-worker-20260914.sh',
    ))
    files.add(RECIPES / 'tests/tooling/test_sparse_capacity_recipe.py')
    if not all(f.is_file() and not f.is_symlink() and f.resolve().is_relative_to(PROJECTS) for f in files):
        raise RuntimeError('Missing, symlinked or out-of-scope evidence')
    raw = ROOT / 'build/large-be-extended-wall-v1.tar.zst'
    compact_pack = ROOT / 'build/large-be-extended-wall-compact-v1.tar.zst'
    if OUT.exists() or raw.exists() or compact_pack.exists():
        raise RuntimeError('New archive outputs required; originals are never overwritten')
    compact = OUT / 'compact'
    compact.mkdir(parents=True)
    manifest = {str(f.relative_to(PROJECTS)): sha(f) for f in sorted(files)}
    for path in sorted(files):
        with path.open('rb') as stream:
            binary = stream.read(8).startswith((b'\x7fELF', b'!<arch>'))
        if binary or path.suffix in ('.o', '.a', '.so', '.zst'):
            continue
        target = compact / 'records' / path.relative_to(PROJECTS)
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(path, target)
    (compact / 'raw-manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    inventory = OUT / 'raw-paths.txt'
    inventory.write_text('\n'.join(manifest) + '\n')
    subprocess.run(['tar', '--use-compress-program=zstd -T2 -3', '-cf', str(raw), '-C', str(PROJECTS),
                    '-T', str(inventory), str(compact.relative_to(PROJECTS))], check=True)
    if any(sha(PROJECTS / name) != digest for name, digest in manifest.items()):
        raise RuntimeError('Files changed while archiving; archive not qualified')
    receipt = dict(path=str(raw), bytes=raw.stat().st_size, sha256=sha(raw), files=len(manifest),
                   scientific_status=record['status'], complete_matrix=len(completed) == 4,
                   completed_harnesses=completed, completed_storage_trajectories=2 * len(completed),
                   formal_timing=False, independent_reaction_oracle=False,
                   collector_stdout_archived=False,
                   original_1800_second_failures_retained=True)
    (compact / 'raw-archive.json').write_text(json.dumps(receipt, indent=2) + '\n')
    subprocess.run(['tar', '--use-compress-program=zstd -T2 -3', '-cf', str(compact_pack),
                    '-C', str(OUT), 'compact'], check=True)
    print(json.dumps(dict(raw=receipt, compact=dict(path=str(compact_pack),
          bytes=compact_pack.stat().st_size, sha256=sha(compact_pack))), indent=2))
    print('LARGE_BE_EXTENDED_WALL_ARCHIVE_PASS_NOT_SCIENCE_PASS')


if __name__ == '__main__':
    main()
