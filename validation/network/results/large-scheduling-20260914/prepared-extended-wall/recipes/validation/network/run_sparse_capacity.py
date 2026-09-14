"""Recompile only the capacity-selectable Host harness; keep factory math frozen.

Focused correctness/capacity evidence, not CPU8/full-application speedup. Native
compile/link recipes and every failed sample are retained in a new output dir.
"""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import shlex
import signal
import subprocess
import time


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def positive_duration(text):
    value = float(text)
    if not math.isfinite(value) or value <= 0:
        raise argparse.ArgumentTypeError('finite positive physical duration required')
    return value


def trajectory_command(exe, method, pool, storage, steps, duration=1e-10):
    return [str(exe), '1e7', '3e9', str(duration), '1e8', '1e-7', str(steps),
            '--ode', method, '--storage-cells', *map(str, storage), '--pool-cells', str(pool),
            'c12=0.5', 'o16=0.5']


def positive_wall_timeout(text):
    try:
        value = float(text)
    except ValueError as error:
        raise argparse.ArgumentTypeError('finite wall timeout in (0, 86400] seconds required') from error
    if not math.isfinite(value) or not 0 < value <= 86400:
        raise argparse.ArgumentTypeError('finite wall timeout in (0, 86400] seconds required')
    return value


def argument_parser():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--build-dir', type=Path, required=True)
    p.add_argument('--source', type=Path, required=True)
    p.add_argument('--provider', type=Path, required=True)
    p.add_argument('--output-dir', type=Path, required=True)
    p.add_argument('--methods', nargs='+', default=['bd', 'ros4'])
    p.add_argument('--pools', type=int, nargs='+', default=[1, 8, 32])
    p.add_argument('--storage', type=int, nargs=2, default=[32, 33])
    p.add_argument('--steps', type=int, default=4)
    p.add_argument('--duration', type=positive_duration, default=1e-10,
                   help='same physical trajectory duration for CPU/GPU; original gate is 1e-10')
    p.add_argument('--preload', type=Path, help='optional test-only native API observer; never formal timing')
    p.add_argument('--run-timeout', type=positive_wall_timeout, default=1800,
                   help='wall-clock limit per trajectory command; default 1800; never changes physical duration or accuracy')
    return p


def main():
    p = argument_parser()
    a = p.parse_args()
    def interrupted(signum, frame):
        raise KeyboardInterrupt(f'interrupted by signal {signum}')
    signal.signal(signal.SIGTERM, interrupted)
    build, source, provider, out = [v.resolve() for v in (a.build_dir, a.source, a.provider, a.output_dir)]
    if out.exists() or min(a.pools + a.storage + [a.steps]) < 1 or max(a.pools) > min(a.storage):
        p.error('new output and valid positive bounded capacity/storage required')
    out.mkdir(parents=True)
    record = dict(status='running', scope='capacity-numerical-diagnostic-not-formal-speedup',
                  duration=a.duration, steps=a.steps,
                  runtime_timeout_seconds=a.run_timeout, build_command_timeout_seconds=1800,
                  recipe_sha256=digest(Path(__file__)),
                  source_sha256=digest(source), provider_sha256=digest(provider),
                  compile_commands_sha256=digest(build / 'compile_commands.json'),
                  artifacts=[], commands=[], runs=[])
    if a.preload:
        a.preload = a.preload.resolve()
        record['observer'] = dict(path=str(a.preload),sha256=digest(a.preload))
    def save():
        (out / 'record.json').write_text(json.dumps(record, indent=2) + '\n')
    def run(name, command, timeout=1800, observe=False):
        row = dict(name=name, command=command, cwd=str(build))
        record['commands'].append(row)
        save()
        start = time.monotonic()
        try:
            with (out / (name + '.stdout')).open('w') as stdout, (out / (name + '.stderr')).open('w') as stderr:
                env = dict(os.environ,LD_PRELOAD=str(a.preload)) if observe and a.preload else None
                rc = subprocess.run(command, cwd=build, stdout=stdout, stderr=stderr, timeout=timeout,env=env).returncode
        except subprocess.TimeoutExpired:
            row.update(returncode=None, timed_out=True, timeout_seconds=timeout,
                       elapsed_seconds=time.monotonic() - start)
            save()
            raise
        row.update(returncode=rc, timed_out=False, timeout_seconds=timeout,
                   elapsed_seconds=time.monotonic() - start)
        save()
        if rc: raise RuntimeError(f'{name}: exit {rc}; logs retained')
    try:
        entries = json.loads((build / 'compile_commands.json').read_text())
        for network in ('audit150', 'audit200'):
            target = 'arch_cuda_generated_sparse_burn_' + network
            selected = [e for e in entries if e['file'].endswith('/test_generated_sparse_burn.cpp')
                        and target + '.dir/' in e['command']]
            if len(selected) != 1: raise RuntimeError('ambiguous Host harness recipe')
            entry = selected[0]
            command = shlex.split(entry['command'])
            command[command.index(entry['file'])] = str(source)
            obj = out / (network + '.o')
            command[command.index('-o') + 1] = str(obj)
            run('compile-' + network, command)
            native = shlex.split(subprocess.run(['ninja', '-t', 'commands', target], cwd=build,
                capture_output=True, text=True, check=True).stdout.splitlines()[-1])
            if native[:2] != [':', '&&'] or native[-2:] != ['&&', ':']:
                raise RuntimeError('unsupported native link recipe')
            command = native[2:-2]
            main_objects = [i for i, v in enumerate(command) if v.endswith('/test_generated_sparse_burn.cpp.o')]
            factory_objects = [Path(v) for v in command if v.endswith('/test_generated_sparse_burn_factory.cu.o')]
            if len(main_objects) != 1 or len(factory_objects) != 1 or command.count('libarch_cuda_sparse_provider.a') != 1:
                raise RuntimeError('ambiguous object/provider binding')
            factory = build / factory_objects[0]
            factory_hash = digest(factory)
            # Record frozen device identity before even the first runtime: a
            # timeout must retain the object used, not only successful runs.
            artifact = dict(network=network, factory_path=str(factory),
                            factory_sha256=factory_hash)
            record['artifacts'].append(artifact)
            save()
            command[main_objects[0]] = str(obj)
            command[command.index('libarch_cuda_sparse_provider.a')] = str(provider)
            exe = out / target
            command[command.index('-o') + 1] = str(exe)
            run('link-' + network, command)
            artifact.update(executable_path=str(exe), executable_sha256=digest(exe))
            save()
            for method in a.methods:
                for pool in a.pools:
                    name = f'{network}-{method}-pool{pool}'
                    command = trajectory_command(exe, method, pool, a.storage, a.steps, a.duration)
                    run(name, command, timeout=a.run_timeout, observe=True)
                    lines = (out / (name + '.stdout')).read_text().splitlines()
                    if 'GENERATED_SPARSE_BURN_PARITY_PASS' not in lines:
                        raise RuntimeError('missing real parity completion')
                    record['runs'].append(dict(name=name, passed=True, executable_sha256=digest(exe),
                        factory_sha256=factory_hash, metrics=[v for v in lines if v.startswith(('metrics,', 'gpu_step,', 'cpu_step,'))]))
                    save()
            if digest(factory) != factory_hash: raise RuntimeError('frozen factory object changed')
        if digest(source) != record['source_sha256'] or digest(provider) != record['provider_sha256']:
            raise RuntimeError('experiment input identity changed')
        if a.preload and digest(a.preload) != record['observer']['sha256']:
            raise RuntimeError('native observer changed')
        if digest(Path(__file__)) != record['recipe_sha256']:
            raise RuntimeError('capacity recipe changed during execution')
        if digest(build / 'compile_commands.json') != record['compile_commands_sha256']:
            raise RuntimeError('native compile recipes changed during execution')
        record['status'] = 'passed'
    except BaseException as error:
        record.update(status='failed', error=repr(error))
        raise
    finally:
        save()


if __name__ == '__main__':
    main()
