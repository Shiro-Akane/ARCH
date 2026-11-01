#!/usr/bin/env python3
"""Summarize every retained sample; distinguish application timing and PMPI diagnostics."""
import argparse, json, statistics
from pathlib import Path
p=argparse.ArgumentParser()
p.add_argument("--cpu",type=Path,required=True)
p.add_argument("--gpu",type=Path,required=True)
p.add_argument("--output",type=Path,required=True)
a=p.parse_args()

def stats(values):
    return {"n":len(values),"median":statistics.median(values),"min":min(values),"max":max(values),
            "cv":statistics.stdev(values)/statistics.mean(values) if len(values)>1 else None}

def read(root,suite):
    data=json.loads((root/(suite+"-summary.json")).read_text())
    assert data["complete"], suite
    assert all(x["verified"] for x in data["archives"])
    assert len(data["archives"])==len(data["samples"])
    return data

cpu=read(a.cpu,"cpu");profile=read(a.cpu,"profile");cross=read(a.gpu,"cross")
out={"cpu":[],"cross":[],"profile":[],"comparisons":{},"interpretation":[]}
for kind in ("strong","weak"):
 for dim in ("2d","3d"):
  base=None
  for ranks in (1,2,4):
   samples=[s for s in cpu["samples"] if s["phase"]=="measured" and
            s["name"].startswith(kind+"-"+dim+"-") and s["ranks"]==ranks]
   assert len(samples)==5
   row={"kind":kind,"dimension":dim,"ranks":ranks,"steps":samples[0]["steps"],
        "shape":samples[0]["shape"],
        "driver_nonoutput":stats([s["metrics"]["critical_driver_nonoutput_seconds"] for s in samples]),
        "launcher_wall":stats([s["command_elapsed_seconds"] for s in samples]),
        "max_rank_output":stats([max(r["output_seconds"] for r in s["metrics"]["ranks"]) for s in samples]),
        "sum_rank_peak_rss_kib":stats([s["metrics"]["sum_rank_peak_rss_kib"] for s in samples]),
        "samples":[{"repeat":s["repeat"],"seconds":s["metrics"]["critical_driver_nonoutput_seconds"]} for s in samples]}
   if ranks==1:base=row["driver_nonoutput"]["median"]
   row["speedup"]=base/row["driver_nonoutput"]["median"]
   row["efficiency"]=row["speedup"]/ranks if kind=="strong" else row["speedup"]
   out["cpu"].append(row)
for dim in ("2d","3d"):
 for ranks in (2,4):
  row={"dimension":dim,"ranks":ranks}
  paired={}
  for placement in ("local","cross"):
   samples=[s for s in cross["samples"] if s["phase"]=="measured" and
            s["name"].startswith(dim+"-") and s["ranks"]==ranks and s["placement"]==placement]
   assert len(samples)==5
   row[placement]=stats([s["metrics"]["critical_driver_nonoutput_seconds"] for s in samples])
   paired[placement]={s["repeat"]:s["metrics"]["critical_driver_nonoutput_seconds"] for s in samples}
  row["paired_cross_over_local"]=stats([paired["cross"][i]/paired["local"][i] for i in paired["local"]])
  out["cross"].append(row)
for data in (profile,cross):
 for sample in data["samples"]:
  if not sample["profiled"]:continue
  ranks=sample["metrics"]["ranks"]
  row={"name":sample["name"],"placement":sample["placement"],"ranks":sample["ranks"],
       "critical_driver_nonoutput_seconds":sample["metrics"]["critical_driver_nonoutput_seconds"],
       "rank_metrics":[]}
  for rank in ranks:
   ops=rank["mpi_profile"]["operations"]
   operations={name:{"calls":value["calls"],"seconds":value["seconds"],
                   "seconds_per_call":value["seconds"]/value["calls"] if value["calls"] else 0,
                   "sent_payload_bytes":value["sent_payload_bytes"]} for name,value in ops.items()}
   mpi_seconds=sum(x["seconds"] for x in operations.values())
   proc=rank["process"]
   ticks_before=proc["core_ticks_before"];ticks_after=proc["core_ticks_after"]
   ticks={c:[b-a for a,b in zip(ticks_before[c],ticks_after[c])] for c in ticks_before}
   row["rank_metrics"].append({"rank":rank["rank"],"host":proc["host"],"affinity":proc["affinity"],
       "operations":operations,"mpi_seconds":mpi_seconds,"stages":rank["stages"],
       "mpi_over_driver_nonoutput":mpi_seconds/rank["driver_nonoutput_seconds"],
       "user_seconds":rank["resources"]["user_seconds"],"system_seconds":rank["resources"]["system_seconds"],
       "core_ticks_delta":ticks})
  # CLOCK_MONOTONIC is shared within one Linux host only.
  if len({r["process"]["host"] for r in ranks})==1:
   entries=[r["mpi_profile"] for r in ranks]
   traces=[r["trace"]["Allgather"] for r in entries]
   assert all(len(t)==sample["steps"] for t in traces)
   records=[]
   for i in range(sample["steps"]):
    arrivals=[r["initialized_monotonic"]+r["trace"]["Allgather"][i][0] for r in entries]
    ends=[arrivals[k]+entries[k]["trace"]["Allgather"][i][1] for k in range(len(entries))]
    records.append({"spread":max(arrivals)-min(arrivals),"tail":max(ends)-max(arrivals)})
   for name,chosen in (("all",records),("after_first",records[1:])):
    spread=sum(v["spread"] for v in chosen);tail=sum(v["tail"] for v in chosen)
    row["allgather_"+name]={"arrival_spread_sum":spread,"completion_tail_sum":tail,
                         "arrival_share_of_summed_call_spans":spread/(spread+tail)}
  out["profile"].append(row)
for name,data in (("cpu",cpu),("profile",profile),("cross",cross)):
 out["comparisons"][name]=sum(s["comparison"] is not None for s in data["samples"])
out["interpretation"]=[
 "Strong scaling fixes total cells and steps. Weak scaling fixes per-rank cells and steps, not physical duration.",
 "Primary metric is max_rank(driver-output); it includes setup, MPI calls and waiting.",
 "PMPI durations include waiting; rank sums and per-stage maxima are not a disjoint wall-time partition.",
 "Cross-host comparisons include differing VM configurations and do not isolate network bandwidth.",
 "No timestamp alignment is performed between different hosts.",
 "Warmups and profile runs are excluded from measured medians but all outputs are validated and retained."]
a.output.write_text(json.dumps(out,indent=2)+"\n")
print(json.dumps({"rows":len(out["cpu"]),"cross_rows":len(out["cross"]),"comparisons":out["comparisons"]}))
