#!/usr/bin/env python3
"""Per-rank measurement wrapper; keeps MPI instrumentation out of the solver."""
import json
import os
from pathlib import Path
import socket
import subprocess
import sys
import time

lane, profiled, executable, *arguments = sys.argv[1:]
rank = int(os.environ.get("OMPI_COMM_WORLD_RANK", "0"))
metrics = Path(lane) / "metrics" / f"rank-{rank}"
metrics.mkdir(parents=True, exist_ok=True)
environment = os.environ.copy()
environment.update(OMP_NUM_THREADS="1", OPENBLAS_NUM_THREADS="1", PYTHONDONTWRITEBYTECODE="1")
if profiled == "1":
    environment["LD_PRELOAD"] = str(Path(__file__).resolve().parents[1] / "bin/libmpi_profile.so")
    environment["ARCH_MPI_PROFILE_DIR"] = str(Path(lane) / "metrics")
started = time.time()
record = {"rank": rank, "host": socket.gethostname(), "affinity": sorted(os.sched_getaffinity(0)),
          "started_utc_epoch": started, "profiled": profiled == "1",
          "load_before": os.getloadavg(), "command": [executable, *arguments]}
result = subprocess.run(["/usr/bin/time", "-f",
    "wall_seconds\t%e\nmax_rss_kib\t%M\nuser_seconds\t%U\nsystem_seconds\t%S\nexit_code\t%x",
    "-o", str(metrics / "resources.tsv"), executable, *arguments],
    env=environment, stdin=subprocess.DEVNULL)
record.update(exit_code=result.returncode, elapsed_seconds=time.time()-started,
              finished_utc_epoch=time.time(), load_after=os.getloadavg())
(metrics / "process.json").write_text(json.dumps(record, indent=2) + "\n")
raise SystemExit(result.returncode)
