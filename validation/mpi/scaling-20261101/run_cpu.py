"""Fixed-grid strong-scaling protocol: one warmup, five unprofiled repetitions."""
import json,math,statistics,traceback
from measure import *
report={"host":socket.gethostname(),"cases":[],"protocol":{"ranks":[1,2,4],"warmups_per_rank":1,"measured_repetitions":5,
 "primary_profiled":False,"diagnostic_profiled_runs_per_rank":1,"timing":"max over ranks of driver_seconds minus output_seconds",
 "stage_timing":"max over ranks of sum of recorded CPU stages","rss":"sum of individual rank peak RSS, not simultaneous aggregate peak",
 "matrix":"fixed total grid and steps per case; one CPU core per MPI rank","baseline":"independent main 5dc6d7bad3fcf37c1aa261d66ec1e8dcf6c5e21d",
 "output":"initial and final plot; serial and MPI1 additionally force a final checkpoint",
 "budget_gib":1.5,"disk_floor_gib":16}}
assert socket.gethostname()=="gen-989544","formal timings are assigned to the idle CPU server"
def save():
    (E/"cpu-summary.json").write_text(json.dumps(report,indent=2))
def size():
    return sum(p.stat().st_size for p in (E/"cpu").rglob("*") if p.is_file())
try:
    preflight()
    for name,shape in (("smooth2d",(16,8,0)),("smooth3d",(8,2,2))):
        root=E/"cpu"/name;root.mkdir(parents=True,exist_ok=False)
        pilot=root/"pilot8";launch(pilot,shape,8,1)
        pm=metrics(pilot)
        # Aim at approximately ten seconds of MPI1 non-output work, capped at 128 steps.
        steps=max(16,min(128,int(math.ceil(10/max(pm["critical_driver_nonoutput_seconds"]/8,0.0001)/8))*8))
        case={"name":name,"shape":shape,"steps":steps,"pilot":pm,"samples":[]}
        report["cases"].append(case);save()
        baseline=root/"baseline";launch(baseline,shape,steps,0,baseline=True)
        reference=comparison.snapshots(baseline/"output")
        case["baseline"]=metrics(baseline)
        print(name,"steps",steps,"pilot",pm["critical_driver_nonoutput_seconds"],flush=True)
        for ranks in (1,2,4):
            lane=root/f"warmup-p{ranks}";launch(lane,shape,steps,ranks)
            case["samples"].append({"phase":"warmup","ranks":ranks,"path":str(lane),"comparison":check(reference,lane),"metrics":metrics(lane)});save()
        for repeat,order in enumerate(((1,2,4),(2,4,1),(4,1,2),(4,2,1),(2,1,4)),1):
            for ranks in order:
                if size()>1.5*2**30:raise RuntimeError("evidence size budget reached")
                lane=root/f"repeat{repeat}-p{ranks}";launch(lane,shape,steps,ranks)
                m=metrics(lane)
                case["samples"].append({"phase":"measured","repeat":repeat,"ranks":ranks,"path":str(lane),"comparison":check(reference,lane),"metrics":m});save()
                print(name,"repeat",repeat,"p",ranks,"seconds",m["critical_driver_nonoutput_seconds"],flush=True)
        for ranks in (1,2,4):
            lane=root/f"profile-p{ranks}";launch(lane,shape,steps,ranks,True)
            m=metrics(lane)
            assert all("mpi_profile" in r for r in m["ranks"]),"observer not loaded"
            case["samples"].append({"phase":"profile","ranks":ranks,"path":str(lane),"comparison":check(reference,lane),"metrics":m});save()
        case["statistics"]={}
        for ranks in (1,2,4):
            selected=[s["metrics"] for s in case["samples"] if s["phase"]=="measured" and s["ranks"]==ranks]
            t=[x["critical_driver_nonoutput_seconds"] for x in selected]
            stage=[x["critical_stage_sum_seconds"] for x in selected]
            case["statistics"][str(ranks)]={"n":len(t),"median_seconds":statistics.median(t),"min_seconds":min(t),"max_seconds":max(t),"cv":statistics.stdev(t)/statistics.mean(t),
             "stage_median_seconds":statistics.median(stage),"median_sum_rank_peak_rss_mib":statistics.median(x["sum_rank_peak_rss_kib"] for x in selected)/1024}
        t1=case["statistics"]["1"]["median_seconds"]
        for p,stats in case["statistics"].items():stats.update(speedup=t1/stats["median_seconds"],parallel_efficiency=t1/stats["median_seconds"]/int(p))
        save()
    report.update(complete=True,evidence_bytes=size(),finished_epoch=time.time());save()
    print("COMPLETE",report["evidence_bytes"],flush=True)
except Exception as exc:
    report.update(complete=False,error=str(exc),failed_epoch=time.time());save();raise
