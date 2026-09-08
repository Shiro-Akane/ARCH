"""Local measurement only: replay identical compiler argv into fresh scratch outputs.

Uses the actual compile database, disables compiler-cache hits, preserves all
optimization/code-image flags and never overwrites the build's objects. The
outer run_memory_guarded process owns resource safety and descendant cleanup.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import json
from pathlib import Path
import shlex
import subprocess
import tempfile
import time


def redirect_outputs(arguments, scratch, index):
    """Redirect the object and explicit dependency outputs into this run's scratch."""
    command = list(arguments)
    unsupported = ('--output-file', '--output-directory', '-odir',
                   '--dependency-output', '--keep', '-keep', '--save-temps',
                   '-save-temps', '--time', '-time', '--fdevice-time-trace')
    for argument in command:
        if argument.startswith(unsupported):
            raise RuntimeError(f'unsupported auxiliary/output option: {argument}')
        if argument.startswith('-o') and argument != '-o':
            raise RuntimeError('use one separate -o output argument')
        if argument.startswith('-MF') and argument != '-MF':
            raise RuntimeError('use a separate -MF dependency output argument')
    for option, suffix, required in (('-o', '.o', True), ('-MF', '.d', False)):
        count = command.count(option)
        if count == 0 and not required:
            continue
        if count != 1:
            raise RuntimeError(f'expected one {option} argument')
        position = command.index(option) + 1
        if position == len(command) or not command[position] or command[position].startswith('-'):
            raise RuntimeError(f'missing value for {option}')
        command[position] = str(scratch / f'route-{index}{suffix}')
    dependency_modes = ('-MD', '-MMD', '--generate-dependencies-with-compile',
                        '--generate-nonsystem-dependencies-with-compile')
    if any(option in command for option in dependency_modes) and '-MF' not in command:
        raise RuntimeError('dependency generation requires an explicit separate -MF output')
    return command


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--build-dir', type=Path, required=True)
parser.add_argument('--jobs', type=int, choices=(1, 2), required=True)
args = parser.parse_args()
database = json.loads((args.build_dir/'compile_commands.json').read_text())
names = ('CudaBackendBurnTabular4DAprox21.cu', 'aprox21_tabular4deosview.cu')
selected = []
for name in names:
    matching = [entry for entry in database if Path(entry['file']).name == name]
    if len(matching) != 1:
        raise RuntimeError(f'expected one compile command for {name}')
    selected.append(matching[0])

with tempfile.TemporaryDirectory(prefix='arch-heavy-compile-') as directory:
    scratch = Path(directory)
    def compile_one(item):
        index, entry = item
        command = entry.get('arguments') or shlex.split(entry['command'])
        # CMake's compilation database normally excludes launchers; reject
        # unexpected tools so a cached invocation cannot look like a cold one.
        if Path(command[0]).name != 'nvcc':
            raise RuntimeError(f'unexpected compiler/launcher: {command[0]}')
        command = redirect_outputs(command, scratch, index)
        print('COMPILE_INPUT '+json.dumps(dict(source=entry['file'], argv=command)), flush=True)
        started = time.monotonic()
        result = subprocess.run(['/usr/bin/time', '-f',
            'CONTROLLED_COMPILE elapsed_seconds=%e peak_rss_kib=%M exit_code=%x',
            *command], cwd=entry['directory'], check=False)
        print('COMPILE_FINISHED '+json.dumps(dict(source=entry['file'],
              elapsed_seconds=time.monotonic()-started, exit_code=result.returncode)), flush=True)
        return result.returncode
    started = time.monotonic()
    with ThreadPoolExecutor(max_workers=args.jobs) as executor:
        codes = list(executor.map(compile_one, enumerate(selected)))
    print('CONTROLLED_GROUP '+json.dumps(dict(jobs=args.jobs, sources=list(names),
          elapsed_seconds=time.monotonic()-started, exit_codes=codes)), flush=True)
    if any(codes):
        raise SystemExit(1)
