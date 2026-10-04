#!/usr/bin/env python3
"""Read a scoped Linux/WSL platform snapshot; never run or configure a workload."""
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
    args = parser.parse_args()
    result = snapshot(args.expected_gpu)
    print(json.dumps(result, indent=2, allow_nan=False))
    return 2 if result["platform_match"] is False else 0

if __name__ == "__main__":
    sys.exit(main())
