"""Capture read-only provenance and machine state for the server experiments."""
from datetime import datetime, timezone
import importlib.metadata
import json
from pathlib import Path
import subprocess
import sys

root = Path('/home/ubuntu/projects/ARCH-perf-20260909')
toolchain = Path('/home/ubuntu/projects/.envs/arch')
commands = {
    'source': ['git', 'rev-parse', 'HEAD'],
    'source_status': ['git', 'status', '--short'],
    'kernel': ['uname', '-a'],
    'cpu': ['lscpu'],
    'gpu': ['nvidia-smi', '-q'],
    'gpu_processes': ['nvidia-smi', '--query-compute-apps=pid,process_name,used_gpu_memory', '--format=csv'],
    'compiler': ['/usr/bin/g++-11', '--version'],
    'cuda_compiler': [str(toolchain / 'bin/nvcc'), '--version'],
    'cmake': [str(toolchain / 'bin/cmake'), '--version'],
    'disk': ['df', '-h', str(root)],
    'memory': ['free', '-m'],
    'processes': ['ps', '-eo', 'pid,pcpu,rss,comm', '--sort=-pcpu'],
    'uv': ['/home/ubuntu/.local/bin/uv', '--version'],
}
for name, executable in [('performance', root / 'build/performance-20260909/release/bin/ARCH'),
                         ('provider', root / 'build/large-network-20260909/release/arch_cuda_cudss_sparse_solver')]:
    if executable.exists():
        commands[name + '_runtime_libraries'] = ['ldd', str(executable)]
result = {'captured_utc': datetime.now(timezone.utc).isoformat(),
          'python': sys.version, 'commands': {},
          'python_packages': {d.metadata['Name']: d.version for d in importlib.metadata.distributions()}}
for name, command in commands.items():
    run = subprocess.run(command, cwd=root, text=True, capture_output=True, timeout=30)
    result['commands'][name] = {'command': command, 'returncode': run.returncode,
                              'stdout': run.stdout, 'stderr': run.stderr}
result['proc'] = {name: Path('/proc', name).read_text() for name in ('stat', 'loadavg', 'meminfo')}
Path(sys.argv[1]).write_text(json.dumps(result, indent=2) + '\n')
