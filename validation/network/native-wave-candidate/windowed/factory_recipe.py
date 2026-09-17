"""Prepare exact, fresh window-factory build recipes, never execute them.

An include-prefix overlay is insufficient: the original SparseBurnCells.cuh
includes SparseOdeBatch.cuh relative to its own directory. Copy the complete
pinned source inventory so quoted includes cannot silently select old execution.
Only the two reviewed execution headers and the two window files differ.
The caller must qualify the standalone contracts before any actual build.
"""
import hashlib
from pathlib import Path, PurePosixPath
import shlex

from prepare_factory import PINS, transform

SOURCE_MANIFEST_SHA = 'af807299a6d4b922657fa17f1139ecdc4231fc8c94ffa6b462a0b69b636ab6da'
WINDOW_PINS = {
    'CuDssSparseWindowSolver.h': 'e6a56e08299fab35c1fd99e35bb611ac45b82a5f237b91fa032dfc99cbf9e7f5',
    'CuDssSparseWindowSolver.cpp': 'a675a6a5b978b077545057f6f09c64519097a7532d7ae7f058ae0c956aa50708',
}


def digest(data):
    return hashlib.sha256(data).hexdigest()


def source_plan(source, manifest_bytes, window):
    """Validate everything before creating a new copy; no existing file edits."""
    if digest(manifest_bytes) != SOURCE_MANIFEST_SHA:
        raise ValueError('unreviewed original source manifest')
    originals, changed = {}, {}
    for line in manifest_bytes.decode().splitlines():
        expected, name = line.split('  ', 1)
        relative = PurePosixPath(name)
        path = Path(source)/name
        if (relative.is_absolute() or '..' in relative.parts or name in originals
                or path.is_symlink() or not path.is_file()
                or not path.resolve().is_relative_to(Path(source).resolve())):
            raise ValueError('unsafe, missing or duplicate original source: '+name)
        data = path.read_bytes()
        if digest(data) != expected:
            raise ValueError('original source changed: '+name)
        originals[name] = expected
        changed[name] = transform(data, name) if name in PINS else data
    if len(originals) != 474 or not set(PINS) <= set(originals):
        raise ValueError('incomplete original source inventory')
    for name, expected in WINDOW_PINS.items():
        path = Path(window)/name
        if path.is_symlink() or not path.is_file():
            raise ValueError('missing or unsafe window source')
        data = path.read_bytes()
        if digest(data) != expected:
            raise ValueError('window source is not the frozen contract candidate')
        destination = 'src/cuda/microphysics/'+name
        if destination in changed:
            raise ValueError('window path overlaps original source')
        changed[destination] = data
    return originals, changed


def materialize_source(source, manifest_bytes, window, output):
    output = Path(output)
    if output.exists() or output.is_symlink():
        raise ValueError('preserve existing private source')
    originals, files = source_plan(source, manifest_bytes, window)
    output.mkdir(parents=True)
    for name, data in files.items():
        target = output/name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
    return {'originals': originals, 'private': {name: digest(data) for name, data in files.items()}}


def strict(argv, cuda):
    required = (['--fmad=false', '--ftz=false', '--prec-div=true', '--prec-sqrt=true',
                 '-Xcompiler=-fno-fast-math,-ffp-contract=off'] if cuda else
                ['-fno-fast-math', '-ffp-contract=off'])
    if (not argv or not argv[0].endswith('/nvcc' if cuda else '/g++-11')
            or any(flag not in argv for flag in required)
            or any(flag in argv for flag in ('-ffast-math', '--use_fast_math'))):
        raise ValueError('original strict compiler recipe changed')


def compile_recipe(entries, network, kind, old_source, private_source, obj, dependency):
    if network not in (150, 200) or kind not in ('factory', 'harness', 'wrapper'):
        raise ValueError('unregistered fresh factory input')
    leaf = {'factory': 'test_generated_sparse_burn_factory.cu',
            'harness': 'test_generated_sparse_burn.cpp', 'wrapper': 'CuDssSparseWaveSolver.cpp'}[kind]
    target = f'arch_cuda_generated_sparse_burn_audit{network}.dir/'
    selected = [row for row in entries if row['file'].endswith('/'+leaf)
                and (kind == 'wrapper' or target in row['command'])]
    if len(selected) != 1:
        raise ValueError('ambiguous original compile recipe')
    row = selected[0]
    argv = shlex.split(row['command'])
    strict(argv, kind == 'factory')
    old, private = str(old_source).rstrip('/'), str(private_source).rstrip('/')
    if (not row['file'].startswith(old+'/') or argv.count('-o') != 1 or argv.count('-c') != 1
            or argv.count(row['file']) != 1 or any(flag in argv for flag in ('-MD', '-MMD', '-MF', '-MT'))):
        raise ValueError('unreviewed source/output/dependency recipe')
    source_name = (row['file'][len(old)+1:] if kind != 'wrapper'
                   else 'src/cuda/microphysics/CuDssSparseWindowSolver.cpp')
    argv[argv.index(row['file'])] = private+'/'+source_name
    argv[argv.index('-o')+1] = str(obj)
    # Preserve all network/solver/FP/architecture settings and external inputs.
    # Project include roots alone move to the complete byte-audited copy.
    argv = [private+value[len(old):] if value.startswith(old+'/') else
            '-I'+private+value[len('-I'+old):] if value.startswith('-I'+old+'/') else value
            for value in argv]
    argv += ['-MD', '-MF', str(dependency), '-MT', str(obj)]
    return dict(command=argv, cwd=row['directory'])


def link_recipe(tokens, harness_obj, factory_obj, wrapper_obj, provider, executable):
    if tokens[:2] != [':', '&&'] or tokens[-2:] != ['&&', ':']:
        raise ValueError('unsupported original link wrapper')
    argv = list(tokens[2:-2])
    strict(argv, False)
    objects = [i for i, value in enumerate(argv) if value.endswith('.o')]
    main = [i for i in objects if argv[i].endswith('/test_generated_sparse_burn.cpp.o')]
    factory = [i for i in objects if argv[i].endswith('/test_generated_sparse_burn_factory.cu.o')]
    if (len(objects) != 2 or len(main) != 1 or len(factory) != 1 or argv.count('-o') != 1
            or argv.count('libarch_cuda_sparse_provider.a') != 1):
        raise ValueError('ambiguous original factory link inputs')
    argv[main[0]], argv[factory[0]] = str(harness_obj), str(factory_obj)
    i = argv.index('libarch_cuda_sparse_provider.a')
    argv[i:i+1] = [str(wrapper_obj), str(provider)]
    argv[argv.index('-o')+1] = str(executable)
    return argv


def verify_factory_dependencies(text, obj, old_source, private_source):
    """Audit actual compiler output, not merely command-line include intent."""
    text = text.replace('\\\r\n', '').replace('\\\n', '')
    if text.count(':') != 1:
        raise ValueError('unreviewed Linux compiler dependency syntax')
    target, body = text.split(':', 1)
    if shlex.split(target) != [str(obj)]:
        raise ValueError('dependency target is not the new CUDA object')
    files = shlex.split(body)
    old, private = str(old_source).rstrip('/'), str(private_source).rstrip('/')
    needed = {private+'/'+name for name in (*PINS,
        'src/cuda/microphysics/SparseBurnCells.cuh',
        'src/cuda/microphysics/CuDssSparseWindowSolver.h')}
    if not needed <= set(files) or any(name.startswith(old+'/') for name in files):
        raise ValueError('new factory resolved an old or missing execution header')
    return files
