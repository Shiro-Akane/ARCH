"""CPU-only continuation checks for the preserved merged mainline.

No GPU/context calls, library changes, physics edits or timing qualification.
The existing CPU-only CMake tree gains previously unbuilt contract targets;
the production ARCH executable must remain byte-identical.
"""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tarfile
import time

BASE = Path('/home/ubuntu/projects')
MERGE = BASE / 'ARCH-main-merge-20260917'
OUTPUT = BASE / 'ARCH-microphysics-resume-20260918/cpu-v2'
SOURCE_SHA = '24dcfae52cf22c0d1d5f78a82069cdf815b32ebdb4a284701f5f9a6344e52590'
HELM_SHA = 'c9a57c26c6fd2b2b378b9d5295ca1214022f6fec6289d038b47bf8c8938881a1'
TESTS = ('sparse_ode_continuation', 'sparse_residual', 'burn_mainline_reference',
         'shared_stage_scheduler', 'mainline_authority', 'helm_components',
         'generated_nse', 'amr_flux_surface_plan', 'compensated_sum')


def sha(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def main():
    if OUTPUT.exists():
        raise ValueError('Preserve existing continuation evidence; never overwrite or silently retry')
    source, build = MERGE / 'source', MERGE / 'cpu-build-v1'
    archive = MERGE / 'source-v2.tar'
    assert sha(archive) == SOURCE_SHA
    cache = (build / 'CMakeCache.txt').read_text()
    assert 'ARCH_ENABLE_CUDA:BOOL=OFF' in cache and 'ARCH_ENABLE_KLU:BOOL=OFF' in cache
    assert 'CMAKE_CXX_COMPILER:STRING=/usr/bin/g++-11' in cache
    # Exact compile inputs, not a guess based on the checkout's branch label.
    inputs = {}
    with tarfile.open(archive) as tar:
        for item in tar:
            if not item.isfile():
                continue
            assert not Path(item.name).is_absolute() and '..' not in Path(item.name).parts
            path = source / item.name
            expected = hashlib.sha256(tar.extractfile(item).read()).hexdigest()
            assert sha(path) == expected, str(path)
            inputs[str(path)] = expected
    table = source / 'EOS_toolkit/tables/helmholtz/helm_table.dat'
    provider = BASE / 'ARCH-native-wave-v4-20260916/source/EOS_toolkit/tables/helmholtz/helm_table.dat'
    assert sha(provider) == HELM_SHA  # Same Git LFS object, not a different EOS table.
    table_was_missing = not table.exists()
    if table_was_missing:
        assert not table.is_symlink()
        table.parent.mkdir(parents=True, exist_ok=True)
        with provider.open('rb') as src, table.open('xb') as dst:
            shutil.copyfileobj(src, dst)
    assert sha(table) == HELM_SHA
    for path in (archive, build / 'CMakeCache.txt', build / 'bin/ARCH', table, Path(__file__)):
        inputs[str(path)] = sha(path)
    OUTPUT.mkdir(parents=True)
    (OUTPUT / 'inputs.json').write_text(json.dumps(inputs, indent=2) + '\n')
    report = dict(status='running', scope='CPU contracts only, not GPU or performance qualification',
                  source_archive_sha256=SOURCE_SHA, selected_tests=TESTS, commands=[],
                  physical_settings_modified=False, libraries_modified=False, gpu_executed=False,
                  helm_runtime_asset=dict(restored_missing_asset=table_was_missing,
                      source=str(provider), destination=str(table), sha256=HELM_SHA))

    def save():
        (OUTPUT / 'report.json').write_text(json.dumps(report, indent=2) + '\n')

    def run(name, command):
        row = dict(name=name, command=command, cwd=str(MERGE), status='running', timeout_seconds=1800)
        report['commands'].append(row)
        save()
        begin = time.monotonic()
        print('CPU_RESUME_STARTED ' + name, flush=True)
        try:
            with (OUTPUT / (name + '.stdout')).open('xb') as out, (OUTPUT / (name + '.stderr')).open('xb') as err:
                r = subprocess.run(command, cwd=MERGE, stdout=out, stderr=err, timeout=1800, check=False)
            row.update(returncode=r.returncode, status='passed' if r.returncode == 0 else 'failed')
            if r.returncode:
                raise RuntimeError('CPU continuation failed: ' + name)
        except BaseException as error:
            row.update(status='failed', error=repr(error))
            raise
        finally:
            row['wall_seconds_diagnostic_only'] = time.monotonic() - begin
            save()
        print('CPU_RESUME_FINISHED ' + name, flush=True)

    envbin = BASE / '.envs/arch/bin'
    os.environ.update(PATH=str(envbin) + os.pathsep + os.environ['PATH'], CCACHE_DISABLE='1',
                      OMP_NUM_THREADS='1', OPENBLAS_NUM_THREADS='1', PYTHONDONTWRITEBYTECODE='1')
    try:
        run('build', ['/usr/bin/time', '-v', 'timeout', '--kill-after=30s', '1700',
                      str(envbin / 'cmake'), '--build', str(build), '--parallel', '1', '--target',
                      *['arch_' + name for name in TESTS]])
        run('ctest', [str(envbin / 'ctest'), '--test-dir', str(build), '--output-on-failure',
                      '--verbose', '--timeout', '300', '-R', '^(' + '|'.join(TESTS) + ')$'])
        for path, expected in inputs.items():
            assert sha(path) == expected, path
        report.update(status='passed', inputs_and_production_binary_unchanged=True)
    except BaseException as error:
        report.update(status='failed', error=repr(error))
        raise
    finally:
        save()


if __name__ == '__main__':
    main()
