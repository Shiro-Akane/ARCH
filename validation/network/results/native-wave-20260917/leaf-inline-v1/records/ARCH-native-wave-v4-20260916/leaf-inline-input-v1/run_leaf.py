"""Isolated generated-network leaf A/B; not an ODE/application qualification.

Only a tiny adapter's inlining annotation differs between private network trees.
Use the archived CMake compile recipe, strict FP, original numerical budget,
two explicit compositions, complete CPU/GPU vectors, and baseline/candidate ABBA.
"""
import hashlib
import json
import math
import os
from pathlib import Path
import re
import shlex
import shutil
import statistics
import struct
import subprocess
import time

from prepare import sink_variant, timed_driver

ROOT = Path('/home/ubuntu/projects/ARCH-native-wave-v4-20260916')
NETWORKS = ROOT.parent / 'ARCH-large-networks-20260909'
COMMANDS_SHA = '6a17407cf53fba1ad868e783245ea21c56274db29829e4eea0dd32bc0ba62049'
VARIANTS = ('baseline', 'inline', 'inline', 'baseline')
GROUPS = ('jacobian', 'rhs', 'energy_gradient', 'temperature_gradient', 'scalars')
BUDGET = 2e-10


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def compile_recipe(rows, network, driver, header, executable):
    matches = [row for row in rows if row['file'] == (ROOT / 'source/tests/cuda/test_generated_network_math.cu').as_posix()
               and f'-DARCH_TEST_NETWORK_TYPE=NetCustom_audit{network} ' in row['command']]
    if len(matches) != 1:
        raise ValueError('missing/ambiguous original math recipe')
    argv = shlex.split(matches[0]['command'])
    required = ('-O3', '-std=c++20', '--fmad=false', '--ftz=false', '--prec-div=true', '--prec-sqrt=true',
                '-Xcompiler=-fno-fast-math,-ffp-contract=off', '-ccbin=/usr/bin/g++-11',
                '--generate-code=arch=compute_90,code=[compute_90,sm_90]')
    if any(token not in argv for token in required) or any(token in argv for token in ('-G', '--use_fast_math')):
        raise ValueError('original strict Release sm90 recipe required')
    for token in ('-c', '-o', '-include'):
        if argv.count(token) != 1:
            raise ValueError('ambiguous compiler action')
    include_index = argv.index('-include') + 1
    if argv[include_index] != (NETWORKS / f'audit{network}/NetCustom_audit{network}.h').as_posix():
        raise ValueError('wrong original network include')
    argv[include_index] = str(header)
    original_driver = (ROOT / 'source/tests/cuda/test_generated_network_math.cu').as_posix()
    if argv.count(original_driver) != 1:
        raise ValueError('wrong original driver')
    argv[argv.index(original_driver)] = str(driver)
    argv[argv.index('-o') + 1] = str(executable)
    argv.remove('-c')  # NVCC links its own CUDA runtime; no sparse-solver/provider calls.
    return ['/usr/bin/time', '-v', *argv]


def snapshot(data, network):
    if len(data) < 24:
        raise ValueError('truncated leaf snapshot')
    magic, neq, nnz = struct.unpack_from('<QQQ', data)
    if magic != 0x415243484C454146 or neq != network + 1 or not 0 < nnz <= neq * neq:
        raise ValueError('wrong leaf snapshot identity/shape/endian')
    position, fields = 24, {}
    for group, expected in zip(GROUPS, (nnz, network, network, network, 3)):
        if position + 8 > len(data):
            raise ValueError('truncated group')
        count, = struct.unpack_from('<Q', data, position)
        position += 8
        if count != expected or position + count * 16 > len(data):
            raise ValueError('wrong/truncated group shape')
        cpu = struct.unpack_from(f'<{count}d', data, position)
        gpu = struct.unpack_from(f'<{count}d', data, position + count * 8)
        position += count * 16
        if not all(math.isfinite(v) for v in (*cpu, *gpu)):
            raise ValueError('nonfinite leaf output')
        error = max((abs(a - b) / max(1.0, abs(a)) for a, b in zip(cpu, gpu)), default=0.0)
        if error > BUDGET:
            raise ValueError('original Host/Device leaf budget exceeded')
        fields[group] = dict(cpu=cpu, gpu=gpu, host_device_error=error)
    if position != len(data) or fields['scalars']['cpu'][2] != 1.0 or fields['scalars']['gpu'][2] != 1.0:
        raise ValueError('trailing bytes or invalid sparse pattern')
    if not any(v != 0.0 for v in fields['rhs']['cpu']) or not any(v != 0.0 for v in fields['jacobian']['cpu']):
        raise ValueError('vacuous network evaluation')
    return dict(neq=neq, nnz=nnz, fields=fields)


def compare_snapshots(reference, actual):
    if (reference['neq'], reference['nnz']) != (actual['neq'], actual['nnz']):
        raise ValueError('baseline/candidate CSR shape differs')
    result = {}
    for field in GROUPS:
        result[field] = {}
        for backend in ('cpu', 'gpu'):
            a, b = reference['fields'][field][backend], actual['fields'][field][backend]
            error = max((abs(x - y) / max(1.0, abs(x)) for x, y in zip(a, b)), default=0.0)
            if len(a) != len(b) or not math.isfinite(error) or error > BUDGET:
                raise ValueError('baseline/candidate original leaf budget exceeded')
            result[field][backend] = dict(max_scaled_error=error, exactly_equal=a == b)
    return result


def parse_output(text, network, composition):
    lines = text.splitlines()
    expected = f'LEAF_INPUT composition={composition} neq={network+1} rho=1e+07 temperature=3e+09'
    if lines.count(expected) != 1 or lines.count('LEAF_SNAPSHOT_WRITTEN endian=little') != 1:
        raise ValueError('missing/extraneous/wrong actual input or snapshot')
    success = [line for line in lines if line.startswith('GENERATED_NETWORK_MATH_PASS ')]
    if len(success) != 1 or not re.fullmatch(
            rf'GENERATED_NETWORK_MATH_PASS neq={network+1} nnz=\d+ rho=10000000 temperature=3000000000', success[0]):
        raise ValueError('missing original parity success')
    resources = [line for line in lines if line.startswith('LEAF_STATIC_RESOURCES ')]
    if len(resources) != 1 or not re.fullmatch(r'LEAF_STATIC_RESOURCES registers=\d+ local_bytes=\d+ shared_bytes=\d+', resources[0]):
        raise ValueError('missing actual kernel resources')
    samples = []
    for line in lines:
        if not line.startswith('LEAF_EVENT_TIMING '):
            continue
        tokens = dict(part.split('=') for part in line.split()[1:])
        if set(tokens) != {'sample', 'repeats', 'cpu_ms', 'gpu_ms'} or int(tokens['sample']) != len(samples) or tokens['repeats'] != '20':
            raise ValueError('incorrect timing repetition sequence')
        row = {name: float(tokens[name]) for name in ('cpu_ms', 'gpu_ms')}
        if not all(math.isfinite(v) and v > 0 for v in row.values()):
            raise ValueError('invalid diagnostic duration')
        samples.append(row)
    if len(samples) != 5:
        raise ValueError('missing or duplicate timing samples')
    return dict(samples=samples, resources=resources[0], nnz=int(success[0].split('nnz=')[1].split()[0]),
                median_gpu_ms=statistics.median(row['gpu_ms'] for row in samples),
                median_cpu_ms=statistics.median(row['cpu_ms'] for row in samples))


def main():
    inputs_dir, out = Path(__file__).resolve().parent, ROOT / 'leaf-inline-v1'
    if out.exists() or os.environ.get('LD_PRELOAD') or os.environ.get('ARCH_SPARSE_ADVANCE_THREADS'):
        raise ValueError('new output and no inherited diagnostic overrides required')
    for name in ('ARCH', 'nvcc', 'ptxas', 'cc1plus'):
        if subprocess.run(['pgrep', '-x', name], capture_output=True).returncode != 1:
            raise ValueError('another build/application active')
    if subprocess.run(['nvidia-smi', '--query-compute-apps=pid', '--format=csv,noheader'],
                      capture_output=True, text=True, check=True).stdout.strip():
        raise ValueError('GPU is not idle')
    if shutil.disk_usage(ROOT).free < 4 * 1024**3:
        raise ValueError('less than 4 GiB disk headroom')
    parent, receipt = ROOT / 'api-cost-collection-v1.json', ROOT / 'api-cost-local-receipt-v1.json'
    p, local = json.loads(parent.read_text()), json.loads(receipt.read_text())
    if not p.get('diagnostic_contracts_pass') or not p.get('diagnostic_numerical_pass') or p['raw'] != local['raw']:
        raise ValueError('previous diagnostic must be completed and archived on both ends')
    commands_path = ROOT / 'factory-release/compile_commands.json'
    if sha(commands_path) != COMMANDS_SHA:
        raise ValueError('original CMake command inventory changed')
    driver_path = ROOT / 'source/tests/cuda/test_generated_network_math.cu'
    driver = timed_driver(driver_path.read_bytes())
    originals = {path for n in (150, 200) for path in (NETWORKS / f'audit{n}').rglob('*') if path.is_file()}
    if len(originals) != 50 or any(path.is_symlink() for path in originals):
        raise ValueError('original complete network inventory required')
    originals.update((parent, receipt, commands_path, driver_path))
    originals.update(path for path in inputs_dir.iterdir() if path.is_file())
    inputs = {str(path): sha(path) for path in sorted(originals)}
    out.mkdir()
    test = out / 'timed_network_math.cu'
    test.write_bytes(driver)
    record = dict(status='running', scope='annotation-only-generated-network-leaf-diagnostic', inputs=inputs,
                  commands=[], runs=[], products={str(test): sha(test)}, network_copies={}, comparisons=[],
                  budget=BUDGET, order=list(VARIANTS), compositions=['co', 'uniform'], rho=1e7, temperature=3e9,
                  performance_qualified=False, application_qualified=False, release_qualified=False)

    def save():
        (out / 'record.json').write_text(json.dumps(record, indent=2) + '\n')

    def run(name, command, env=None, wall=12000):
        row = dict(name=name, command=command, cwd=str(ROOT / 'factory-release'), timeout_seconds=wall,
                   environment={key: env[key] for key in ('ARCH_LEAF_COMPOSITION', 'ARCH_LEAF_SNAPSHOT')} if env else {})
        record['commands'].append(row)
        save()
        print('LEAF_STARTED ' + name, flush=True)
        start = time.monotonic()
        try:
            with (out / (name + '.stdout')).open('w') as stdout, (out / (name + '.stderr')).open('w') as stderr:
                code = subprocess.run(command, cwd=row['cwd'], env=env, stdout=stdout, stderr=stderr, timeout=wall).returncode
        except subprocess.TimeoutExpired:
            row.update(returncode=None, timed_out=True, elapsed_seconds=time.monotonic()-start)
            save()
            raise
        row.update(returncode=code, timed_out=False, elapsed_seconds=time.monotonic()-start)
        save()
        if code:
            raise RuntimeError(f'{name} exit {code}; preserve first failure')
        print('LEAF_FINISHED ' + name, flush=True)

    try:
        for network in (150, 200):
            executables = {}
            for variant in ('baseline', 'inline'):
                source = NETWORKS / f'audit{network}'
                target = out / f'audit{network}-{variant}'
                shutil.copytree(source, target)
                if variant == 'inline':
                    adapter = target / f'NetCustom_audit{network}.math.h'
                    adapter.write_bytes(sink_variant(adapter.read_bytes(), network))
                copied = {path.relative_to(target).as_posix(): sha(path) for path in target.rglob('*') if path.is_file()}
                source_files = {path.relative_to(source).as_posix(): sha(path) for path in source.rglob('*') if path.is_file()}
                changed = [name for name in copied if copied[name] != source_files[name]]
                if set(copied) != set(source_files) or changed != ([] if variant == 'baseline' else [f'NetCustom_audit{network}.math.h']):
                    raise ValueError('private network copy contains unintended changes')
                record['network_copies'][str(target)] = copied
                exe = out / f'leaf-audit{network}-{variant}'
                command = compile_recipe(json.loads(commands_path.read_text()), network, test,
                                         target / f'NetCustom_audit{network}.h', exe)
                run(f'compile-audit{network}-{variant}', command)
                record['products'][str(exe)] = sha(exe)
                executables[variant] = exe
                save()
            for composition in ('co', 'uniform'):
                reference = None
                results = []
                for ordinal, variant in enumerate(VARIANTS):
                    name = f'audit{network}-{composition}-{ordinal}-{variant}'
                    binary = out / (name + '.bin')
                    env = dict(os.environ, ARCH_LEAF_COMPOSITION=composition, ARCH_LEAF_SNAPSHOT=str(binary))
                    run(name, [str(executables[variant]), '1e7', '3e9'], env=env, wall=300)
                    result = parse_output((out / (name + '.stdout')).read_text(), network, composition)
                    vectors = snapshot(binary.read_bytes(), network)
                    if vectors['nnz'] != result['nnz']:
                        raise ValueError('snapshot/transcript CSR shape differs')
                    reference = reference or vectors
                    comparison = compare_snapshots(reference, vectors)
                    record['products'][str(binary)] = sha(binary)
                    result.update(name=name, variant=variant, composition=composition, network=network,
                                  comparison_to_first_baseline=comparison,
                                  host_device_errors={key: value['host_device_error'] for key, value in vectors['fields'].items()})
                    record['runs'].append(result)
                    results.append(result)
                    save()
                medians = {v: statistics.median(row['median_gpu_ms'] for row in results if row['variant'] == v)
                           for v in ('baseline', 'inline')}
                record['comparisons'].append(dict(network=network, composition=composition, median_gpu_ms=medians,
                                                  baseline_over_inline=medians['baseline']/medians['inline']))
        for path, digest in {**inputs, **record['products']}.items():
            if sha(path) != digest:
                raise ValueError('input/product identity changed')
        for directory, copied in record['network_copies'].items():
            current = {p.relative_to(directory).as_posix(): sha(p) for p in Path(directory).rglob('*') if p.is_file()}
            if current != copied:
                raise ValueError('private complete network inventory changed')
        record.update(status='passed', identities_verified_after=True, diagnostic_numerical_pass=True)
    except BaseException as error:
        record.update(status='failed', error=repr(error), diagnostic_numerical_pass=False)
        raise
    finally:
        save()
    print('LEAF_DIAGNOSTIC_PASS two_actual_networks four_ABBA_pairs no_application_qualification', flush=True)


if __name__ == '__main__':
    main()
