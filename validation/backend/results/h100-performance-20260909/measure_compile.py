"""CMake compiler launcher: retain commands and GNU time per-invocation RSS.

GNU time max RSS is not simultaneous aggregate RSS.
The existing system memory guard separately observes combined memory pressure.
"""
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import time

output = Path(sys.argv[1])
command = sys.argv[2:]
output.mkdir(parents=True, exist_ok=True)
key = hashlib.sha256("\0".join(command).encode()).hexdigest()[:20]
record = {"command": command, "started_utc": datetime.now(timezone.utc).isoformat(),
          "working_directory": str(Path.cwd()), "status": "running",
          "rss_scope": "maximum child process RSS, not aggregate concurrent RSS"}
path = output / (key + ".json")
if path.exists():
    path = output / (key + "-" + str(time.time_ns()) + ".json")
path.write_text(json.dumps(record, indent=2) + "\n")
start = time.monotonic()
metric = path.with_suffix('.metric')
result = subprocess.run(['/usr/bin/time', '-o', str(metric), '-f',
    'ARCH_COMPILE_METRIC elapsed_seconds=%e peak_rss_kib=%M exit_code=%x command=%C',
    *command])
record.update(status="passed" if result.returncode == 0 else "failed",
              returncode=result.returncode, seconds=time.monotonic() - start,
              gnu_time_metric_file=str(metric),
              finished_utc=datetime.now(timezone.utc).isoformat())
path.write_text(json.dumps(record, indent=2) + "\n")
if metric.exists():
    # The repository's existing summarize_cuda_compile_memory.py owns parsing.
    print(metric.read_text(), end='', file=sys.stderr, flush=True)
raise SystemExit(result.returncode)
