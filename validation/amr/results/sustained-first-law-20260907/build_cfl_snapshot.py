"""Compile a diagnostic with the candidate's existing C++ flags and libraries.

Only the diagnostic source and output paths differ. Existing build objects,
candidate executables and maintained CMake configuration are never replaced.
Run under the memory guard; this is not an application qualification record.
"""
import argparse
import json
from pathlib import Path
import shlex
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(ROOT / 'tools'))
from validate_backend_results import run_arch_with_logs, require_empty_output_root
import validation_provenance as provenance

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--build-dir', type=Path, required=True)
parser.add_argument('--output-dir', type=Path, required=True)
args = parser.parse_args()
build, output = args.build_dir.resolve(), args.output_dir.resolve()
require_empty_output_root(output)
output.mkdir(parents=True, exist_ok=True)
source = Path(__file__).with_name('cfl_snapshot.cpp').resolve()
object_file, binary = output / 'cfl_snapshot.o', output / 'cfl_snapshot'
entries = json.loads((build / 'compile_commands.json').read_text())
entry, = [item for item in entries if item['file'].endswith('/src/io/chk/ChkIO.cpp')]
command = shlex.split(entry['command'])
command[command.index('-c') + 1] = str(source)
command[command.index('-o') + 1] = str(object_file)
query = subprocess.run(['ninja', '-C', str(build), '-t', 'commands', 'arch_cuda_single_level_validation'],
    check=True, capture_output=True, text=True)
link = shlex.split(query.stdout.splitlines()[-1])
if link[:2] != [':', '&&'] or link[-2:] != ['&&', ':']:
    raise RuntimeError('unrecognized configured link wrapper')
link = link[2:-2]
for flag in command:
    if flag.startswith('-fopenmp') and flag not in link:
        link.append(flag)
previous = 'CMakeFiles/arch_cuda_single_level_validation.dir/tests/cuda/test_cuda_single_level_validation.cpp.o'
link[link.index(previous)] = str(object_file)
link[link.index('-o') + 1] = str(binary)
link += [str(build / 'CMakeFiles/ARCH.dir' / (path + '.o')) for path in (
    'src/io/chk/ChkIO.cpp', 'src/io/chk/CheckpointCompatibility.cpp', 'src/core/FileFingerprint.cpp')]
inputs = {str(index): (build / value).resolve() for index, value in enumerate(link)
          if (build / value).is_file()}
inputs['arch'] = build / 'bin/ARCH'
inputs['validator'] = build / 'arch_cuda_single_level_validation'
def identity():
    result = provenance.capture_focused(source_root=ROOT, build_dir=build,
        artifacts={name: path for name, path in inputs.items() if path.is_relative_to(build)})
    result['external_link_inputs'] = {name: provenance.file_identity(path)
        for name, path in inputs.items() if not path.is_relative_to(build)}
    return result
before = identity()
for name, invocation in (('compile', command), ('link', link)):
    lane = output / name
    lane.mkdir()
    result = run_arch_with_logs(invocation, source_root=build, lane_root=lane, timeout=300)
    if result.returncode:
        raise RuntimeError(name + ' failed; logs retained in ' + str(lane))
provenance.require_unchanged(before, identity())
(output / 'build.json').write_text(json.dumps(dict(identity=before, source=provenance.file_identity(source),
    binary=provenance.file_identity(binary), compile=command, link=link), indent=2) + '\n')
print('CFL snapshot probe built: ' + str(binary))
