#!/usr/bin/env python3
"""Capture local, non-GPU merge verification without rewriting earlier results."""
import json
from pathlib import Path
import subprocess
import sys

here = Path(__file__).resolve().parent
root = here.parents[3]
output = here / 'local-checks'
output.mkdir(exist_ok=False)
commands = [
    ('git-preservation', [sys.executable, str(here / 'verify_merge.py')]),
    ('architecture', [sys.executable, 'tools/audit_architecture.py', '.']),
    ('architecture-tests', [sys.executable, '-m', 'unittest', 'discover',
                            '-s', 'tests/tooling', '-p', 'test_audit_architecture.py', '-v']),
]
results = []
for label, command in commands:
    completed = subprocess.run(command, cwd=root, capture_output=True, timeout=180)
    (output / f'{label}.stdout').write_bytes(completed.stdout)
    (output / f'{label}.stderr').write_bytes(completed.stderr)
    results.append({'label': label, 'command': command, 'returncode': completed.returncode})
(output / 'commands.json').write_text(json.dumps(results, indent=2) + '\n')
if any(result['returncode'] for result in results):
    raise SystemExit(1)
print('all local checks passed; no GPU qualification added')
