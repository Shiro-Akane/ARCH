#!/usr/bin/env python3
"""Longer fixed-grid MPI trials; raw outputs stay on the participating servers."""
import argparse, hashlib, importlib.util, json, os, shlex, shutil, socket
import statistics, subprocess, sys, tarfile, time
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument("--evidence", type=Path, required=True)
parser.add_argument("--work", type=Path, required=True, help="Server tmpfs staging directory")
parser.add_argument("--suite", choices=["cpu", "cross", "profile", "smoke", "paired-cpu", "paired-cross"], required=True)
parser.add_argument("--repeats", type=int, default=5)
parser.add_argument("--ssh-config", type=Path, help="JSON argv for already-authorized server SSH")
parser.add_argument("--candidate", default="ARCH")
parser.add_argument("--source-json", type=Path)
parser.add_argument("--peer", default="ubuntu@150.65.181.189")
args = parser.parse_args()
E, W = args.evidence.resolve(), args.work.resolve()
assert str(W).startswith("/dev/shm/arch-mpi-controlled-")
assert args.repeats >= 5
T = Path(json.loads((E / "source.json").read_text()).get("toolchain", "/home/ubuntu/projects/ARCH-mpi-toolchain/env"))
os.environ["ARCH_MPI_EVIDENCE"] = str(E)
sys.path.insert(0, str(E / "tools"))
import measure as m

# Existing parsers/comparators are reused; only the run storage and launch policy differ.
m.E = W
ENV = dict(m.ENV, PATH=str(T / "bin")+":"+os.environ["PATH"], LD_LIBRARY_PATH=str(T / "lib"))
ENV["ARCH_MPI_PROFILE_LIBRARY"] = str(E / "bin/libmpi_trace.so")
ssh = json.loads(args.ssh_config.read_text()) if args.ssh_config else None
transfer_env = dict(os.environ)
transfer_env.pop("LD_LIBRARY_PATH", None)
report = {"suite": args.suite, "host": socket.gethostname(), "started": time.time(),
          "repeats": args.repeats, "output_medium": "server tmpfs",
          "source": json.loads((args.source_json or E / "source.json").read_text()), "samples": [],
          "archives": [], "complete": False}
report_path = E / (args.suite + "-summary.json")

def save():
    temp = report_path.with_suffix(".tmp")
    temp.write_text(json.dumps(report, indent=2))
    temp.replace(report_path)

def transfer(items):
    subprocess.run(["rsync", "-a", "-e", shlex.join(ssh), *items], env=transfer_env,
                   stdin=subprocess.DEVNULL, check=True, timeout=120)

def preflight():
    floor = 20 if socket.gethostname() == "gpu-273312" else 16
    assert shutil.disk_usage(E).free > floor * 2**30, "persistent disk reserve"
    assert shutil.disk_usage(W).free > 4 * 2**30, "RAM output reserve"
    return {"host": socket.gethostname(), "epoch": time.time(), "load": os.getloadavg(),
            "disk_free": shutil.disk_usage(E).free, "ram_free": shutil.disk_usage(W).free,
            "processes": subprocess.run(["ps", "-eo", "pid,comm,pcpu", "--sort=-pcpu"],
                capture_output=True, text=True).stdout.splitlines()[:10]}

def archive(lane):
    target = E / "archives" / (lane.name + ".tar.gz")
    assert not target.exists()
    files = {str(p.relative_to(lane)): {"bytes": p.stat().st_size,
             "sha256": hashlib.sha256(p.read_bytes()).hexdigest()}
             for p in sorted(lane.rglob("*")) if p.is_file()}
    with tarfile.open(target, "w:gz", compresslevel=3) as tar:
        tar.add(lane, arcname=lane.name)
    checked = set()
    with tarfile.open(target, "r:gz") as tar:
        for member in tar:
            if not member.isfile():
                continue
            name = str(Path(member.name).relative_to(lane.name))
            data = tar.extractfile(member).read()
            assert len(data) == files[name]["bytes"]
            assert hashlib.sha256(data).hexdigest() == files[name]["sha256"]
            checked.add(name)
    assert checked == set(files)
    report["archives"].append({"name": target.name, "bytes": target.stat().st_size,
        "sha256": hashlib.sha256(target.read_bytes()).hexdigest(), "members": files,
        "verified": True})
    save()
    # Only this run's verified staging directory is removed.
    assert lane.parent == W and lane.name.startswith(args.suite + "-")
    shutil.rmtree(lane)

def mpi_command(ranks, placement):
    base = [str(T / "bin/mpiexec"), "--bind-to", "core", "--report-bindings"]
    for variable in ("PATH", "LD_LIBRARY_PATH", "OMP_NUM_THREADS", "OPENBLAS_NUM_THREADS",
                     "ARCH_MPI_PROFILE_LIBRARY"):
        base += ["-x", variable]
    if placement == "cross":
        base += ["--host", "150.65.181.188:2,150.65.181.189:2",
                 "--map-by", f"ppr:{ranks // 2}:node", "--prtemca", "plm_ssh_agent",
                 shlex.join(["/usr/bin/env", "-u", "LD_LIBRARY_PATH", *ssh]),
                 "--prtemca", "oob_tcp_include", "150.65.181.0/24",
                 "--mca", "btl", "self,tcp", "--mca", "btl_tcp_if_include", "150.65.181.0/24"]
    else:
        base += ["--map-by", "pe-list=" + ",".join(map(str, range(ranks))) + ":ordered"]
    return [*base, "-n", str(ranks)]

def run(name, shape, steps, ranks, placement="local", phase="measured", reference=None,
        profiled=False, variant=None, repeat=None):
    variant = variant or args.candidate
    lane = W / (args.suite + "-" + name)
    par = m.prepare(lane, shape, steps)
    before = preflight()
    if placement == "cross":
        transfer([str(lane), args.peer + ":" + str(W) + "/"])
    wrapped = [str(T / "bin/python"), "-B", str(E / "tools/rank_wrapper.py"),
               str(lane), str(int(profiled)), str(E / "bin" / variant),
               "SmoothAdvection", str(par)]
    cmd = ["taskset", "-c", "0", *wrapped] if ranks == 0 else [*mpi_command(ranks, placement), *wrapped]
    (lane / "launch.json").write_text(json.dumps({"command": cmd, "shape": shape,
        "steps": steps, "placement": placement, "phase": phase, "preflight": before}, indent=2))
    started = time.monotonic()
    with (lane / "run.log").open("w") as stream:
        p = subprocess.run(cmd, env=ENV, stdin=subprocess.DEVNULL, stdout=stream,
                           stderr=subprocess.STDOUT, timeout=900)
    elapsed = time.monotonic() - started
    (lane / "completion.json").write_text(json.dumps({"returncode": p.returncode,
        "command_elapsed_seconds": elapsed, "after": preflight()}, indent=2))
    assert p.returncode == 0, str(lane)
    if placement == "cross":
        # No --delete: the peer contributes only its rank-local output and metrics.
        transfer([args.peer + ":" + str(lane) + "/", str(lane) + "/"])
    snapshots = m.comparison.snapshots(lane / "output")
    comparison = m.comparison.compare(reference, snapshots) if reference else None
    metrics = m.metrics(lane)
    actual = sorted((lane / "output").glob("rank-*")) or [lane / "output"]
    for output in actual:
        rows = m.rows(output / "cpu_stage_timings.tsv")
        hydro = next(row for row in rows if row["stage"] == "hydro")
        assert int(hydro["calls"]) == steps, hydro
    assert len(metrics["ranks"]) == max(ranks, 1)
    pairs = [(r["process"]["host"], tuple(r["process"]["affinity"])) for r in metrics["ranks"]]
    assert len(set(pairs)) == len(pairs)
    assert all(len(pair[1]) == 1 for pair in pairs)
    if placement == "cross":
        assert {p[0] for p in pairs} == {"gpu-273312", "gen-989544"}
    sample = {"name": name, "shape": shape, "steps": steps, "ranks": ranks,
              "placement": placement, "phase": phase, "repeat": repeat, "variant": variant,
              "profiled": profiled, "command_elapsed_seconds": elapsed,
              "comparison": comparison, "metrics": metrics}
    if profiled:
        assert all(r["mpi_profile"]["operations"]["Allgather"]["calls"] == steps
                   for r in metrics["ranks"] if ranks > 1)
    if profiled and variant in ("ARCH-before", "ARCH-concurrent"):
        for rank in metrics["ranks"]:
            operations = rank["mpi_profile"]["operations"]
            if variant == "ARCH-concurrent":
                assert operations["Sendrecv"]["calls"] == 0
                assert operations["Irecv"]["calls"] == operations["Isend"]["calls"] == 2*operations["Waitall"]["calls"]
                assert operations["Waitall"]["calls"] > 0
            else:
                assert operations["Sendrecv"]["calls"] > 0 and operations["Waitall"]["calls"] == 0
    report["samples"].append(sample)
    archive(lane)
    if placement == "cross":
        # Verification above covered the union including peer output; scope is explicit.
        cleanup = ("import pathlib,shutil; p=pathlib.Path(" + repr(str(lane)) + "); "
                   "assert p.parent==pathlib.Path(" + repr(str(W)) + "); shutil.rmtree(p)")
        subprocess.run([*ssh, args.peer, "python3 -c " + shlex.quote(cleanup)],
                       env=transfer_env, stdin=subprocess.DEVNULL, check=True)
    print(name, phase, round(metrics["critical_driver_nonoutput_seconds"], 5), "PASS", flush=True)
    return snapshots

E.joinpath("archives").mkdir(exist_ok=True)
W.mkdir(exist_ok=True)
for part in ("tools", "inputs", "bin"):
    link = W / part
    if not link.exists():
        link.symlink_to(E / part, target_is_directory=True)
workloads = [("2d", (16, 8, 0), 768), ("3d", (8, 2, 2), 192)]
if args.suite.startswith("paired-"):
    workloads = [("2d", (16, 8, 0), 512), ("3d", (8, 2, 2), 128)]
references = {}
if args.suite == "cpu":
    for dimension, fixed, steps in workloads:
        for kind in ("strong", "weak"):
            shapes = {p: fixed if kind == "strong" else
                      ((4*p, 8, 0) if dimension == "2d" else (2*p, 2, 2)) for p in (1, 2, 4)}
            for shape in dict.fromkeys(shapes.values()):
                key = (shape, steps)
                if key not in references:
                    references[key] = run(f"baseline-{dimension}-{'x'.join(map(str,shape))}",
                        shape, steps, 0, phase="baseline", variant="ARCH-baseline")
            for repeat in range(args.repeats + 1):
                for ranks in ((1, 2, 4) if repeat % 2 == 0 else (4, 2, 1)):
                    shape = shapes[ranks]
                    run(f"{kind}-{dimension}-r{repeat}-p{ranks}", shape, steps, ranks,
                        phase="warmup" if repeat == 0 else "measured", repeat=repeat,
                        reference=references[(shape, steps)])
elif args.suite == "cross":
    smoke_reference = run("preflight-baseline", (4, 2, 0), 4, 0,
                          phase="baseline", variant="ARCH-baseline")
    for ranks in (2, 4):
        run(f"preflight-p{ranks}", (4, 2, 0), 4, ranks, placement="cross",
            phase="transport-preflight", reference=smoke_reference, profiled=True)
    for dimension, shape, steps in workloads:
        reference = run("baseline-" + dimension, shape, steps, 0,
                        phase="baseline", variant="ARCH-baseline")
        for repeat in range(args.repeats + 1):
            for ranks in (2, 4):
                for placement in (("local", "cross") if repeat % 2 == 0 else ("cross", "local")):
                    run(f"{dimension}-r{repeat}-p{ranks}-{placement}", shape, steps, ranks,
                        placement=placement, phase="warmup" if repeat == 0 else "measured",
                        repeat=repeat, reference=reference)
        for ranks in (2, 4):
            run(f"{dimension}-p{ranks}-cross-profile", shape, steps, ranks, placement="cross",
                phase="profile", reference=reference, profiled=True)
elif args.suite in ("paired-cpu", "paired-cross"):
    placement = "local" if args.suite == "paired-cpu" else "cross"
    for dimension, shape, steps in workloads:
        reference = run("baseline-" + dimension, shape, steps, 0,
                        phase="baseline", variant="ARCH-baseline")
        for repeat in range(args.repeats + 1):
            for ranks in ((2, 4) if repeat % 2 == 0 else (4, 2)):
                for version in (("before", "concurrent") if repeat % 2 == 0 else ("concurrent", "before")):
                    run(f"{dimension}-r{repeat}-p{ranks}-{version}", shape, steps, ranks,
                        placement=placement, phase="warmup" if repeat == 0 else "measured",
                        repeat=repeat, reference=reference, variant="ARCH-" + version)
        for version in ("before", "concurrent"):
            run(f"{dimension}-p4-{version}-profile", shape, steps, 4,
                placement=placement, phase="profile", reference=reference,
                variant="ARCH-" + version, profiled=True)
elif args.suite == "smoke":
    for dimension, shape, steps in [("2d", (4, 2, 0), 8), ("3d", (4, 1, 1), 4)]:
        reference = run("baseline-" + dimension, shape, steps, 0, phase="baseline", variant="ARCH-baseline")
        run(dimension+"-p2", shape, steps, 2, reference=reference, profiled=True, phase="profile")
else:
    for dimension, shape, steps in workloads:
        reference = run("baseline-" + dimension, shape, steps, 0,
                        phase="baseline", variant="ARCH-baseline")
        for repeat in range(3):
            run(f"{dimension}-r{repeat}-p4-profile", shape, steps, 4, phase="profile",
                repeat=repeat, reference=reference, profiled=True)
report["complete"] = True
report["finished"] = time.time()
save()
print("COMPLETE", args.suite, flush=True)
