"""Measure an isolated cold ARCH build, a no-op and a one-route rebuild.

Configure a NEW build tree with the final candidate's flags/providers/networks.
This recipe invokes the existing memory guard separately for each build phase;
do not run another heavy workload concurrently. CCACHE_DISABLE=1 is required.
Only the mtime of an explicitly selected build-generated route is touched; no
maintained source contents, user objects or cache entries are removed.
"""
import argparse
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import re
import shlex
import sys

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / 'tools'))
import validation_provenance as provenance
from summarize_cuda_compile_memory import parse_commands
from validate_backend_results import require_empty_output_root, run_arch_with_logs


def require_cold_objects(database):
    """Check every configured compile output, including PCH/test objects."""
    if not database:
        raise RuntimeError('cold compile database is empty')
    for entry in database:
        command = entry.get('arguments') or shlex.split(entry['command'])
        if command.count('-o') != 1:
            raise RuntimeError('cannot establish a cold compile output')
        artifact = Path(entry['directory']) / command[command.index('-o') + 1]
        if artifact.exists():
            raise RuntimeError(f'cold build already contains a compiled object: {artifact}')
    return dict(configured_compile_outputs=len(database), existing_compile_outputs=0)


def ninja_contract(build, lane):
    """Observe actual ARCH commands and heavy scheduling without building."""
    lane.mkdir(parents=True)
    files = ('CMakeCache.txt', 'compile_commands.json', 'build.ninja', 'CMakeFiles/rules.ninja')
    configuration = {name: provenance.file_identity(build / name) for name in files}
    command = ['ninja', '-C', str(build), '-t', 'commands', 'ARCH']
    process = run_arch_with_logs(command, source_root=ROOT, lane_root=lane, timeout=60)
    if process.returncode or not process.stdout.strip():
        raise RuntimeError('cannot observe complete Ninja ARCH commands; logs retained')
    # Do not sort, tokenize, strip flags or normalize external dependency roots.
    # Only each tree's own absolute output/generated-file prefix may differ.
    normalized = lane / 'normalized-commands.txt'
    normalized.write_text(process.stdout.replace(str(build), '<BUILD>'), encoding='utf-8')
    rules = (build / 'CMakeFiles/rules.ninja').read_text()
    depths = re.findall(r'^pool arch_cuda_heavy\n\s+depth = ([1-9][0-9]*)\s*$', rules, re.MULTILINE)
    edges, edge = [], None
    for line in (build / 'build.ninja').read_text().splitlines():
        if line.startswith('build '):
            edge = line.split(': ', 1)[0][len('build '):]
        elif line.strip() == 'pool = arch_cuda_heavy':
            if edge is None:
                raise RuntimeError('heavy pool assignment lacks a build edge')
            edges.append(edge.replace(str(build), '<BUILD>'))
    if len(depths) != 1 or not edges:
        raise RuntimeError('missing or ambiguous Ninja heavy-pool configuration')
    if configuration != {name: provenance.file_identity(build / name) for name in files}:
        raise RuntimeError('Ninja configuration changed during command observation')
    return dict(command=command, command_count=len(process.stdout.splitlines()),
        normalized_commands=provenance.file_identity(normalized), configuration=configuration,
        heavy_pool=dict(depth=int(depths[0]), edges=edges))


def require_build_equivalence(build, reference, lane):
    contracts = {name: ninja_contract(tree, lane / name)
                 for name, tree in (('measurement', build), ('reference', reference))}
    left, right = contracts.values()
    if left['normalized_commands']['sha256'] != right['normalized_commands']['sha256']:
        raise RuntimeError('measurement and reference Ninja ARCH commands differ')
    if left['heavy_pool'] != right['heavy_pool']:
        raise RuntimeError('measurement and reference Ninja heavy scheduling differ')
    return contracts


def require_unchanged_contract(before, after):
    for name in before:
        left, right = before[name], after[name]
        if left['normalized_commands']['sha256'] != right['normalized_commands']['sha256'] \
                or left['heavy_pool'] != right['heavy_pool'] \
                or left['configuration'] != right['configuration']:
            raise RuntimeError('Ninja commands or scheduling changed during measurement')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--reference-build', type=Path, required=True)
    parser.add_argument('--incremental-source', type=Path, required=True)
    parser.add_argument('--comparison-source', type=Path,
        help='second generated route for an optional identical-work 1-job versus --jobs measurement')
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--jobs', type=int, required=True)
    parser.add_argument('--min-available-mib', type=int, default=1536)
    parser.add_argument('--max-swap-growth-mib', type=int, default=256)
    args = parser.parse_args()
    if os.environ.get('CCACHE_DISABLE') != '1' or args.jobs < 1:
        parser.error('use CCACHE_DISABLE=1 and a positive job limit')
    build, reference, output = args.build_dir.resolve(), args.reference_build.resolve(), args.output_dir.resolve()
    selected = args.incremental_source.resolve()
    comparison = args.comparison_source.resolve() if args.comparison_source else None
    if build == reference or not selected.is_relative_to(build / 'generated') or not selected.is_file():
        raise ValueError('incremental source must be an existing generated route in the separate measurement build')
    database = json.loads((build / 'compile_commands.json').read_text())
    cold_objects = require_cold_objects(database)
    if not database or not any(Path(entry['file']).resolve() == selected for entry in database):
        raise RuntimeError('selected incremental route is absent from the compile database')
    if comparison is not None and (args.jobs <= 1 or comparison == selected
            or not comparison.is_relative_to(build / 'generated') or not comparison.is_file()
            or not any(Path(entry['file']).resolve() == comparison for entry in database)):
        raise ValueError('comparison requires --jobs > 1 and a second configured generated route')
    require_empty_output_root(output)
    output.mkdir(parents=True, exist_ok=True)
    preflight = require_build_equivalence(build, reference, output / 'preflight')
    def reference_identity():
        return provenance.capture_focused(source_root=ROOT, build_dir=reference,
            artifacts={'arch': reference / 'bin/ARCH'})
    before, recipe = reference_identity(), provenance.file_identity(Path(__file__).resolve())
    configuration = {name: provenance.file_identity(build / name)
                     for name in ('CMakeCache.txt', 'compile_commands.json')}
    route_identity = provenance.file_identity(selected)
    comparison_identity = provenance.file_identity(comparison) if comparison else None
    started, phases = datetime.now(timezone.utc).isoformat(), []
    schedule = [('cold', args.jobs), ('noop', args.jobs), ('incremental', args.jobs)]
    if comparison:
        schedule += [('pair_serial', 1), ('pair_parallel', args.jobs)]
    for phase, jobs in schedule:
        lane = output / phase
        lane.mkdir()
        contract_before = require_build_equivalence(build, reference, lane / 'equivalence-before')
        require_unchanged_contract(preflight, contract_before)
        if phase == 'cold':
            require_cold_objects(database)
        pair = phase.startswith('pair_')
        touched = [selected, comparison] if pair else [selected]
        if phase == 'incremental' or pair:
            touch = output / ('touch-' + phase)
            touch.mkdir()
            process = run_arch_with_logs(['cmake', '-E', 'touch', *(str(path) for path in touched)],
                source_root=ROOT, lane_root=touch, timeout=60)
            if process.returncode or route_identity != provenance.file_identity(selected) \
                    or (comparison and comparison_identity != provenance.file_identity(comparison)):
                raise RuntimeError('incremental setup failed or changed generated source contents')
        log = lane / 'build.log'
        command = [sys.executable, '-B', str(ROOT / 'tools/run_memory_guarded.py'),
            '--min-available-mib', str(args.min_available_mib),
            '--max-swap-growth-mib', str(args.max_swap_growth_mib), '--pressure-guard',
            '--log', str(log), '--', 'cmake', '--build', str(build),
            '--target', 'ARCH', '--parallel', str(jobs)]
        result = run_arch_with_logs(command, source_root=ROOT, lane_root=lane, timeout=14400)
        if result.returncode:
            raise RuntimeError(f'{phase} build failed; logs retained')
        contract_after = require_build_equivalence(build, reference, lane / 'equivalence-after')
        require_unchanged_contract(preflight, contract_after)
        lines = log.read_text().splitlines()
        summaries = [line for line in lines if line.startswith('MEMORY_GUARD_RESULT ')]
        commands = parse_commands(lines)
        if len(summaries) != 1 or 'guard_stopped=False' not in summaries[0] \
                or any(row['exit_code'] for row in commands):
            raise RuntimeError(f'{phase} lacks a successful complete resource/build observation')
        if (phase == 'noop' and commands) or (phase != 'noop' and not commands):
            raise RuntimeError(f'{phase} performed the wrong compile work')
        if phase == 'incremental' and not any(
                Path(row['translation_unit']).resolve() == selected for row in commands):
            raise RuntimeError('incremental measurement did not compile the selected route')
        if pair and sorted(Path(row['translation_unit']).resolve() for row in commands) != sorted(touched):
            raise RuntimeError('paired concurrency measurement compiled different work')
        identity = provenance.capture_focused(source_root=ROOT, build_dir=build,
            artifacts={'arch': build / 'bin/ARCH'})
        if identity['source'] != before['source'] or identity['build']['registered_networks'] \
                != before['build']['registered_networks']:
            raise RuntimeError('measurement build source or generated-network assets differ from the candidate')
        phases.append(dict(name=phase, jobs=jobs, command=command, resource_summary=summaries[0],
            compiler_commands=commands, log=provenance.file_identity(log), identity=identity,
            equivalence_before=contract_before, equivalence_after=contract_after))
        print(f'core build measurement PASS: {phase}; {summaries[0]}', flush=True)
    provenance.require_unchanged(before, reference_identity())
    if recipe != provenance.file_identity(Path(__file__).resolve()) \
            or configuration != {name: provenance.file_identity(build / name) for name in configuration} \
            or route_identity != provenance.file_identity(selected) \
            or (comparison and comparison_identity != provenance.file_identity(comparison)):
        raise RuntimeError('measurement recipe, configuration or generated contents changed')
    evidence = dict(schema=1, scope='isolated optimized core compilation',
        focused_gate_pass=True, release_qualified=False, reference_identity=before,
        configuration=configuration, recipe=recipe, incremental_source=route_identity,
        cold_objects=cold_objects, preflight=preflight,
        comparison_source=comparison_identity,
        compiler_cache_disabled=True, jobs=args.jobs, phases=phases,
        started_utc=started, finished_utc=datetime.now(timezone.utc).isoformat(),
        note='Cold ARCH target includes required dependencies and registered routes, not the test executables. No-op, one-route rebuild and optional identical-work paired concurrency are distinct workloads. Paired times include the unchanged optimized final link; configured heavy-pool limits still apply.')
    (output / 'evidence.json').write_text(json.dumps(evidence, indent=2) + '\n')


if __name__ == '__main__':
    main()
