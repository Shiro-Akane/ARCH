"""Archive finished focused window trajectories without compiling or running."""
import importlib.util
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys

ROOT = Path('/home/ubuntu/projects/ARCH-native-wave-v4-20260916')
HERE = Path(__file__).resolve().parent
parent = ROOT/'input/collect_factory.py' if (ROOT/'input').is_dir() else HERE.parent/'collect_factory.py'
spec = importlib.util.spec_from_file_location('window_focused_archive_helpers', parent)
archive = importlib.util.module_from_spec(spec)
spec.loader.exec_module(archive)
sys.path.insert(0, str(ROOT/'window-focused-input-v1' if (ROOT/'window-focused-input-v1').is_dir() else HERE))
from run_focused import factory_gate, validate_transcript, verify_factory_identity


def main():
    control, output, factory = ROOT/'window-focused-control-v1', ROOT/'window-focused-v1', ROOT/'window-factory-v1'
    code = archive.completion(control)
    for name in ('ARCH', 'nvcc', 'ptxas', 'cc1plus', 'window-test',
                 'arch_cuda_generated_sparse_burn_audit150', 'arch_cuda_generated_sparse_burn_audit200'):
        if subprocess.run(['pgrep', '-f', '^.*/'+re.escape(name)+r'( |$)'], capture_output=True).returncode != 1:
            raise ValueError('compute remains active or process check failed')
    if subprocess.run(['nvidia-smi', '--query-compute-apps=pid', '--format=csv,noheader'],
                      capture_output=True, text=True, check=True).stdout.strip():
        raise ValueError('GPU remains active; collection must wait')
    parents = factory_gate(ROOT)
    built = verify_factory_identity(factory)
    record_path = output/'record.json'
    record = json.loads(record_path.read_text()) if record_path.is_file() else {}
    result = dict(profile='focused', worker_exit_code=code, trajectory_matrix_pass=False,
                  paged_ode_qualified=False, application_qualified=False, performance_qualified=False,
                  release_qualified=False)
    if record:
        try:
            result.update(validate_transcript(record, output, factory, code))
        except Exception as error:
            if code == 0:
                raise
            result['validation_error'] = repr(error)
    if code == 0:
        if (result.get('trajectory_matrix_pass') is not True
                or record.get('identities_verified_after') is not True
                or record.get('trajectory_matrix_pass') is not True or record.get('owners_verified') is not True):
            raise ValueError('success without complete qualified focused trajectories')
        archive.successful_guard(control/'window-focused-guard.log')
    paths = {path for folder in (control, output, ROOT/'window-focused-input-v1')
             for path in folder.rglob('*') if path.is_file()}
    external = {}
    for group in (record.get('inputs', {}), built['inputs'], built['artifacts'], *built['dependencies'].values()):
        for name, expected in group.items():
            path = Path(name)
            if archive.sha(path) != expected:
                raise ValueError('recorded input/product/dependency changed: '+name)
            if path.resolve().is_relative_to(ROOT):
                paths.add(path)
            else:
                external[name] = dict(resolved=str(path.resolve()), bytes=path.stat().st_size, sha256=expected)
    for name in ('source-files', 'network-files', 'vendor', 'artifacts'):
        manifest = ROOT/'factory-control-v2'/(name+'.sha256')
        items = archive.inventory(manifest, ROOT/'source')
        paths.add(manifest)
        if name in ('network-files', 'vendor'):
            for path, expected in items.items():
                external[str(path)] = dict(resolved=str(path.resolve()), bytes=path.stat().st_size, sha256=expected)
    archive.inventory(control/'recipes.sha256', ROOT/'source')
    paths.update((*parents, factory/'record.json', ROOT/'input/collect_factory.py', Path(__file__).resolve()))
    paths.add(factory/'source/tests/cuda/test_generated_sparse_burn.cpp')
    if any(p.is_symlink() or not p.is_file() or not p.resolve().is_relative_to(ROOT) for p in paths):
        raise ValueError('unsafe focused archive input')
    # Compiler dependency aliases remain in the record, not as duplicate tar
    # members/hard links. No relaxation of the download verifier is permitted.
    paths = {p.resolve(strict=True) for p in paths}
    evidence = ROOT/'window-focused-evidence-v1'
    raw, packed = ROOT/'window-focused-raw-v1.tar.zst', ROOT/'window-focused-compact-v1.tar.zst'
    receipt = ROOT/'window-focused-collection-v1.json'
    if any(path.exists() for path in (evidence, raw, packed, receipt)):
        raise ValueError('preserve existing/partial focused archive')
    compact = evidence/'compact'
    compact.mkdir(parents=True)
    manifest = {path.relative_to(ROOT.parent).as_posix(): dict(bytes=path.stat().st_size, sha256=archive.sha(path))
                for path in sorted(paths)}
    omitted = []
    private = factory/'source'
    changed = {'src/cuda/microphysics/SparseOdeBatch.cuh', 'src/cuda/runtime/burn/CudaBackendBurnSparseImpl.cuh',
               'src/cuda/microphysics/CuDssSparseWindowSolver.h', 'src/cuda/microphysics/CuDssSparseWindowSolver.cpp',
               'tests/cuda/test_generated_sparse_burn.cpp'}
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
            raise ValueError('focused evidence changed during collection')
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
