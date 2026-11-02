#!/usr/bin/env python3
"""Summarize the frozen 16-rank protocol; no missing samples or selective timing."""
import argparse
import hashlib
import json
from pathlib import Path
import statistics

parser = argparse.ArgumentParser()
parser.add_argument("--evidence", type=Path, required=True)
parser.add_argument("--output", type=Path, required=True)
args = parser.parse_args()
data = json.loads((args.evidence / "scale-up-summary.json").read_text())
protocol = json.loads((args.evidence / "scale-up-protocol.json").read_text())
assert data["complete"] and data["suite"] == "scale-up"
assert data["repeats"] == protocol["measured_repeats"] == 5
assert len(data["samples"]) == len(data["archives"]) == 62
assert len({s["name"] for s in data["samples"]}) == 62
for archive in data["archives"]:
    path = args.evidence / "archives" / archive["name"]
    assert archive["verified"] and path.stat().st_size == archive["bytes"]
    with path.open("rb") as stream:
        digest = hashlib.sha256()
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
        assert digest.hexdigest() == archive["sha256"]


def stats(values):
    return dict(n=len(values), median=statistics.median(values), min=min(values),
                max=max(values), cv=statistics.stdev(values) / statistics.mean(values))


report = {"scope": protocol["scope"], "protocol": protocol, "source": data["source"],
          "correctness_comparisons": 0, "verified_archives": len(data["archives"]),
          "rows": [], "samples": [], "limits": [
              "Shared single-host VM; other jobs and hypervisor scheduling are not controlled.",
              "Strong scaling only; no weak-scaling or multi-node claim.",
              "Median max_rank(driver-output), not launcher or full-job end-to-end speedup.",
              "Five repeats describe this run window; no statistical significance claim."]}
for workload in protocol["workloads"]:
    dim = workload["name"]
    baseline = [s for s in data["samples"] if s["name"] == "baseline-" + dim]
    assert len(baseline) == 1 and baseline[0]["phase"] == "baseline"
    for ranks in protocol["ranks"]:
        samples = [s for s in data["samples"] if s["name"].startswith(dim + "-")
                   and s["ranks"] == ranks]
        assert len(samples) == 6
        assert sorted(s["repeat"] for s in samples) == list(range(6))
        for sample in samples:
            assert sample["shape"] == workload["root_shape"]
            assert sample["steps"] == workload["steps"]
            assert sample["comparison"]["bitwise_equal"]
            assert sample["comparison"]["cells"] == workload["cells"]
            assert sample["comparison"]["snapshots"] == 2
            assert sample["phase"] == ("warmup" if sample["repeat"] == 0 else "measured")
            assert not sample["profiled"] and sample["placement"] == "local"
            assert len(sample["metrics"]["ranks"]) == ranks
            report["correctness_comparisons"] += 1
        timed = [s for s in samples if s["phase"] == "measured"]
        seconds = stats([s["metrics"]["critical_driver_nonoutput_seconds"] for s in timed])
        if ranks == 1:
            reference = seconds["median"]
        report["rows"].append(dict(dimension=dim, ranks=ranks, driver_nonoutput=seconds,
            speedup=reference / seconds["median"], efficiency=reference / seconds["median"] / ranks,
            launcher_wall=stats([s["command_elapsed_seconds"] for s in timed]),
            sum_rank_peak_rss_kib=stats([s["metrics"]["sum_rank_peak_rss_kib"] for s in timed])))
        for s in samples:
            report["samples"].append(dict(name=s["name"], phase=s["phase"], repeat=s["repeat"],
                seconds=s["metrics"]["critical_driver_nonoutput_seconds"],
                launcher_wall=s["command_elapsed_seconds"], comparison=s["comparison"],
                ranks=s["metrics"]["ranks"]))
assert report["correctness_comparisons"] == 60
args.output.write_text(json.dumps(report, indent=2) + "\n")
print(json.dumps({"comparisons": report["correctness_comparisons"], "rows": report["rows"]}))
