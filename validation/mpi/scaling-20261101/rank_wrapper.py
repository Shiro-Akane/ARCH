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
    environment["LD_PRELOAD"] = os.environ.get("ARCH_MPI_PROFILE_LIBRARY", str(Path(__file__).resolve().parents[1] / "bin/libmpi_profile.so"))
    environment["ARCH_MPI_PROFILE_DIR"] = str(Path(lane) / "metrics")
def core_ticks():
    selected = {f"cpu{cpu}" for cpu in os.sched_getaffinity(0)}
    return {parts[0]: list(map(int, parts[1:9]))
            for line in Path("/proc/stat").read_text().splitlines()
            if (parts := line.split()) and parts[0] in selected}

started = time.time()
before_ticks = core_ticks()
record = {"rank": rank, "host": socket.gethostname(), "affinity": sorted(os.sched_getaffinity(0)),
          "started_utc_epoch": started, "profiled": profiled == "1",
          "load_before": os.getloadavg(), "command": [executable, *arguments],
          "core_ticks_before": before_ticks, "ticks_per_second": os.sysconf("SC_CLK_TCK")}
result = subprocess.run(["/usr/bin/time", "-f",
    "wall_seconds\t%e\nmax_rss_kib\t%M\nuser_seconds\t%U\nsystem_seconds\t%S\nexit_code\t%x",
    "-o", str(metrics / "resources.tsv"), executable, *arguments],
    env=environment, stdin=subprocess.DEVNULL)
record.update(exit_code=result.returncode, elapsed_seconds=time.time()-started,
              finished_utc_epoch=time.time(), load_after=os.getloadavg(), core_ticks_after=core_ticks())
(metrics / "process.json").write_text(json.dumps(record, indent=2) + "\n")
raise SystemExit(result.returncode)
