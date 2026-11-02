#!/usr/bin/env python3
"""Compare complete before/after application pairs, keeping every measured sample."""
import argparse,json,statistics
from pathlib import Path
p=argparse.ArgumentParser()
p.add_argument("--cpu",type=Path,required=True)
p.add_argument("--cross",type=Path,required=True)
p.add_argument("--output",type=Path,required=True)
a=p.parse_args()

def stats(values):
 return {"n":len(values),"median":statistics.median(values),"min":min(values),"max":max(values),
         "cv":statistics.stdev(values)/statistics.mean(values) if len(values)>1 else None}

out={"rows":[],"comparisons":{},"profiles":[],"sources":{}}
for placement,root,suite in [("CPU local",a.cpu,"paired-cpu"),("two hosts",a.cross,"paired-cross")]:
 d=json.loads((root/(suite+"-summary.json")).read_text())
 assert d["complete"] and all(x["verified"] for x in d["archives"])
 assert len(d["samples"])==len(d["archives"])
 assert all(s["comparison"] is None or s["comparison"]["bitwise_equal"] for s in d["samples"])
 out["comparisons"][suite]=sum(s["comparison"] is not None for s in d["samples"])
 out["sources"][suite]=d["source"]
 for dim in ("2d","3d"):
  for ranks in (2,4):
   row={"placement":placement,"dimension":dim,"ranks":ranks}
   pairs={}
   for variant in ("ARCH-before","ARCH-concurrent"):
    ss=[s for s in d["samples"] if s["phase"]=="measured" and s["name"].startswith(dim+"-")
        and s["ranks"]==ranks and s["variant"]==variant]
    assert len(ss)==5
    pairs[variant]={s["repeat"]:s for s in ss}
    row[variant]={"driver_nonoutput":stats([s["metrics"]["critical_driver_nonoutput_seconds"] for s in ss]),
        "launcher_wall":stats([s["command_elapsed_seconds"] for s in ss]),
        "sum_rank_peak_rss_kib":stats([s["metrics"]["sum_rank_peak_rss_kib"] for s in ss])}
   row["pairs"]=[]
   for repeat in sorted(pairs["ARCH-before"]):
    before=pairs["ARCH-before"][repeat]["metrics"]["critical_driver_nonoutput_seconds"]
    after=pairs["ARCH-concurrent"][repeat]["metrics"]["critical_driver_nonoutput_seconds"]
    row["pairs"].append({"repeat":repeat,"before":before,"concurrent":after,"ratio":after/before})
   row["paired_ratio"]=stats([p["ratio"] for p in row["pairs"]])
   row["faster_pairs"]=sum(p["concurrent"]<p["before"] for p in row["pairs"])
   row["median_change_percent"]=100*(row["ARCH-concurrent"]["driver_nonoutput"]["median"]/
                                     row["ARCH-before"]["driver_nonoutput"]["median"]-1)
   out["rows"].append(row)
 for sample in d["samples"]:
  if not sample["profiled"]:continue
  out["profiles"].append({"name":sample["name"],"placement":placement,"variant":sample["variant"],
      "driver_nonoutput":sample["metrics"]["critical_driver_nonoutput_seconds"],
      "ranks":[{"rank":r["rank"],"host":r["process"]["host"],"operations":r["mpi_profile"]["operations"],
                "driver_nonoutput":r["driver_nonoutput_seconds"],"stages":r["stages"]}
               for r in sample["metrics"]["ranks"]]})
for name in ("ARCH-before","ARCH-concurrent","ARCH-baseline"):
 assert out["sources"]["paired-cpu"]["binary_sha256"][name]==out["sources"]["paired-cross"]["binary_sha256"][name]

# Equal payload and exchange counts establish that the candidate sends the same data.
out["halo_payload_checks"]=[]
for placement in ("CPU local", "two hosts"):
 for dim in ("2d", "3d"):
  versions={p["variant"]:p for p in out["profiles"]
            if p["placement"]==placement and p["name"].startswith(dim+"-")}
  before={r["rank"]:r["operations"] for r in versions["ARCH-before"]["ranks"]}
  after={r["rank"]:r["operations"] for r in versions["ARCH-concurrent"]["ranks"]}
  assert before.keys()==after.keys()
  for rank,old in before.items():
   new=after[rank]
   assert old["Sendrecv"]["calls"]==new["Isend"]["calls"]==new["Irecv"]["calls"]
   assert old["Sendrecv"]["sent_payload_bytes"]==new["Isend"]["sent_payload_bytes"]
   assert new["Isend"]["calls"]==2*new["Waitall"]["calls"]
  out["halo_payload_checks"].append({"placement":placement,"dimension":dim,"ranks":len(before),"passed":True})
out["notes"]=[
 "Before is the integrated 814b8118 executable; concurrent changes only the MPI halo exchange region.",
 "Primary medians exclude warmups and PMPI profiles. All saved outputs, including warmups, are compared bitwise.",
 "Ratios below one favor the candidate; show all five paired ratios and unfiltered timing ranges.",
 "CPU-local and two-host experiments are different placements; compare before/after only within each placement.",
 "MPI posted calls and Waitall are recorded; no computation is overlapped with outstanding requests.",
 "The existing send/receive scratch, tags, count validation, numerical kernels and MPI-OFF controls are preserved."]
a.output.write_text(json.dumps(out,indent=2)+"\n")
print(json.dumps({"rows":len(out["rows"]),"comparisons":out["comparisons"]}))
