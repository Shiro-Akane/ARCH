#!/usr/bin/env python3
"""Read Linux/WSL observations, optionally a standalone OpenMP probe; never run ARCH physics."""
import argparse
import csv
import hashlib
import json
import math
import os
from pathlib import Path
import shutil
import subprocess
import sys
from datetime import datetime, timezone

QUERY = "uuid,name,memory.total,memory.used,power.draw,temperature.gpu,utilization.gpu"
METRICS = ("memory_total_mib", "memory_used_mib", "power_draw_w", "temperature_c", "utilization_percent")

def parse_gpus(text):
    records = []
    for row in csv.reader(text.splitlines(), skipinitialspace=True):
        if len(row) != 7 or not row[0].startswith("GPU-") or not row[1].strip():
            raise ValueError("Malformed GPU telemetry row")
        item = {"uuid": row[0], "name": row[1].strip(), "scope": "whole_device"}
        for key, raw in zip(METRICS, row[2:]):
            if raw.strip() in ("N/A", "[N/A]", "[Not Supported]"):
                item[key] = {"state": "unavailable", "reason": raw.strip()}
            else:
                value = float(raw)
                if not math.isfinite(value) or value < 0:
                    raise ValueError("Invalid GPU telemetry metric")
                item[key] = {"state": "observed", "value": value}
        total, used = item["memory_total_mib"], item["memory_used_mib"]
        if total["state"] == "observed" and (total["value"] <= 0 or
                used["state"] == "observed" and used["value"] > total["value"]):
            raise ValueError("Inconsistent GPU memory")
        util = item["utilization_percent"]
        if util["state"] == "observed" and util["value"] > 100:
            raise ValueError("Invalid GPU utilization")
        if any(x["uuid"] == item["uuid"] for x in records):
            raise ValueError("Duplicate GPU identity")
        records.append(item)
    if not records:
        raise ValueError("No GPU rows")
    return records

def parse_openmp_probe(text, requested_threads, allowed):
    result=json.loads(text)
    n=result["team_size"]
    if type(n) is not int or n != requested_threads or n < 1:
        raise ValueError("Actual OpenMP team differs from explicit budget")
    members=result["members"]
    if (len(members)!=n or any(type(m["thread"]) is not int for m in members)
        or sorted(m["thread"] for m in members)!=list(range(n))):
        raise ValueError("Missing or duplicate OpenMP team member")
    for member in members:
        cpus=member["affinity"]
        if (member["affinity_readable"] is not True or not cpus
            or any(type(cpu) is not int or cpu not in allowed for cpu in cpus)
            or len(set(cpus))!=len(cpus) or type(member["cpu"]) is not int
            or member["cpu"] not in cpus):
            raise ValueError("Unreadable/out-of-budget OpenMP affinity")
        if type(member["place"]) is not int or not 0<=member["place"]<result["num_places"]:
            raise ValueError("OpenMP binding place unavailable")
    if type(result["num_places"]) is not int or result["num_places"]<1:
        raise ValueError("OpenMP places unavailable")
    if len(set(m["place"] for m in members))!=n:
        raise ValueError("Requested spread/threads probe shares a place")
    if type(result["proc_bind"]) is not int or result["proc_bind"]<=0:
        raise ValueError("OpenMP binding disabled")
    return result

def observe_openmp_probe(executable, threads, allowed):
    if type(threads) is not int or not 1<=threads<=len(allowed):
        raise ValueError("Probe threads must fit current allowed logical CPUs")
    executable=Path(executable).resolve(strict=True)
    env=os.environ.copy()
    env.update(OMP_NUM_THREADS=str(threads),OMP_DYNAMIC="FALSE",
               OMP_PLACES="threads",OMP_PROC_BIND="spread")
    result={"scope":"Standalone OpenMP team, not ARCH production kernel observation",
            "requested_threads":threads,"allowed_logical_cpus":sorted(allowed),
            "executable_sha256":hashlib.sha256(executable.read_bytes()).hexdigest(),
            "environment":{key:env[key] for key in ("OMP_NUM_THREADS","OMP_DYNAMIC","OMP_PLACES","OMP_PROC_BIND")}}
    try:
        run=subprocess.run([str(executable)],env=env,capture_output=True,text=True,timeout=5)
        if run.returncode:raise ValueError("OpenMP probe failed")
        result.update(state="observed",observation=parse_openmp_probe(run.stdout,threads,set(allowed)))
    except (OSError,ValueError,KeyError,TypeError,subprocess.TimeoutExpired) as error:
        result.update(state="unavailable",reason=type(error).__name__+": "+str(error))
    return result

def snapshot(expected_gpu=None):
    result = {"schema_version": 1, "captured_utc": datetime.now(timezone.utc).isoformat(),
              "scope": "instantaneous platform preflight, not benchmark or peak",
              "cpu": {}, "gpu": {}, "memory": {},
              "limitations": ["Logical CPUs are not physical cores or P/E topology.",
                  "Whole-device GPU values include unrelated applications.",
                  "One snapshot cannot prove idle state, peaks or workload attribution.",
                  "No CPU/CUDA scientific or performance acceptance."]}
    cpu = result["cpu"]
    cpu["model_names"] = sorted(set(line.split(":", 1)[1].strip()
        for line in Path("/proc/cpuinfo").read_text().splitlines() if line.startswith("model name")))
    cpu["allowed_logical_cpus"] = sorted(os.sched_getaffinity(0))
    policies = sorted(Path("/sys/devices/system/cpu/cpufreq").glob("policy*"))
    cpu["frequency_policy"] = {"state": "unavailable", "reason": "cpufreq policies not exposed"}
    if policies:
        entries = []
        for policy in policies:
            values = {}
            for key in ("scaling_driver", "scaling_governor", "scaling_min_freq", "scaling_max_freq"):
                try:
                    values[key] = {"state": "observed", "value": (policy/key).read_text().strip()}
                except OSError:
                    values[key] = {"state": "unavailable", "reason": "sysfs counter unreadable"}
            entries.append({"policy": policy.name, "values": values})
        cpu["frequency_policy"] = {"state": "observed", "policies": entries}
    mem = dict((line.split(":", 1)[0], int(line.split(":", 1)[1].split()[0]))
               for line in Path("/proc/meminfo").read_text().splitlines())
    result["memory"] = {"scope": "Linux guest system", "total_kib": mem["MemTotal"],
                       "available_kib": mem["MemAvailable"], "swap_used_kib": mem["SwapTotal"]-mem["SwapFree"]}
    executable = shutil.which("nvidia-smi")
    gpu = result["gpu"]
    if executable is None:
        gpu.update(state="unavailable", reason="nvidia-smi not available")
    else:
        command = [executable, "--query-gpu="+QUERY, "--format=csv,noheader,nounits"]
        gpu["tool_sha256"] = hashlib.sha256(Path(executable).read_bytes()).hexdigest()
        gpu["query"] = QUERY
        try:
            completed = subprocess.run(command, capture_output=True, text=True, timeout=5)
            gpu["exit_code"] = completed.returncode
            if completed.returncode != 0:
                gpu.update(state="unavailable", reason="nvidia-smi query failed")
            else:
                gpu.update(state="observed", devices=parse_gpus(completed.stdout))
        except subprocess.TimeoutExpired:
            gpu.update(state="unavailable", reason="nvidia-smi query timed out")
        except (OSError, ValueError) as error:
            gpu.update(state="unavailable", reason=type(error).__name__+": invalid or unreadable telemetry")
    result["expected_gpu_name"] = expected_gpu
    result["platform_match"] = (None if expected_gpu is None else
        gpu.get("state") == "observed" and
        any(device["name"] == expected_gpu for device in gpu["devices"]))
    return result

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--expected-gpu", help="exact device name from the frozen platform plan")
    parser.add_argument("--openmp-probe",type=Path,help="trusted standalone probe executable; never ARCH")
    parser.add_argument("--probe-threads",type=int,help="explicit logical-thread budget for the standalone probe")
    args = parser.parse_args()
    if (args.openmp_probe is None)!=(args.probe_threads is None):
        parser.error("--openmp-probe and --probe-threads must be supplied together")
    result = snapshot(args.expected_gpu)
    if args.openmp_probe is not None:
        try:
            result["openmp_probe"]=observe_openmp_probe(args.openmp_probe,args.probe_threads,
                result["cpu"]["allowed_logical_cpus"])
        except (OSError,ValueError) as error:
            parser.error(str(error))
    print(json.dumps(result, indent=2, allow_nan=False))
    return 2 if (result["platform_match"] is False or
        result.get("openmp_probe",{}).get("state")=="unavailable") else 0

if __name__ == "__main__":
    sys.exit(main())
