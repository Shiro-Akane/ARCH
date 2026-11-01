"""Exercise the dedicated preflight limits without waiting production budgets."""
from pathlib import Path
import subprocess
import sys
import time

binary = str(Path(sys.argv[1]).resolve())
for mode in ['complete', 'wall', 'cpu']:
    start = time.monotonic()
    result = subprocess.run([binary, mode], capture_output=True, timeout=12)
    elapsed = time.monotonic() - start
    if mode == 'complete':
        assert result.returncode == 0, (mode, result.returncode, result.stderr)
    else:
        assert result.returncode not in (0, 2), (mode, result.returncode, result.stderr)
        assert elapsed < 10, (mode, elapsed)
print('execution preflight CPU/wall limits and cancellation: ok')
