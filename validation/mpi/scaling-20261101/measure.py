#!/usr/bin/env python3
"""Server-only MPI measurement. All primary timings use an unmodified binary."""
import csv,importlib.util,json,os,shutil,socket,subprocess,sys,time
from pathlib import Path
E=Path(os.environ.get("ARCH_MPI_EVIDENCE","/home/ubuntu/projects/ARCH-mpi-evidence-scaling-20261101"))
T=Path("/home/ubuntu/projects/ARCH-mpi-toolchain/env")
spec=importlib.util.spec_from_file_location("comparison",E/"tools/mpi_compare.py")
comparison=importlib.util.module_from_spec(spec);spec.loader.exec_module(comparison)
ENV=dict(os.environ,PATH=str(T/"bin")+":"+os.environ["PATH"],LD_LIBRARY_PATH=str(T/"lib"),
         OMP_NUM_THREADS="1",OPENBLAS_NUM_THREADS="1",PYTHONDONTWRITEBYTECODE="1")
def preflight(floor=16):
    free=shutil.disk_usage(E).free/2**30
    if free<floor:raise RuntimeError(f"disk floor: {free:.2f} GiB < {floor}")
    return {"host":socket.gethostname(),"epoch":time.time(),"disk_free_gib":free,
            "load":os.getloadavg(),"cpu_count":os.cpu_count(),
            "processes":subprocess.run(["ps","-eo","pid,comm,pcpu","--sort=-pcpu"],capture_output=True,text=True).stdout.splitlines()[:9]}
def prepare(lane,shape,steps,problem="SmoothAdvection"):
    lane.mkdir(parents=True,exist_ok=False)
    n=sum(x>0 for x in shape); roots=shape[0]*max(shape[1],1)*max(shape[2],1)
    values={"nblockx1":shape[0],"nblockx2":shape[1],"nblockx3":shape[2],
        "max_blocks":roots,"lrefinemin":0,"lrefinemax":0,"compute_backend":"cpu",
        "max_steps":steps,"tmax":10,"plt_dstep":steps,"plt_dt":-1,"chk_dstep":-1,"chk_dt":-1,
        "out_dir":str(lane/"output"),"base_name":"MPIScaling",
        "plt_variables":"DENS, PRES, VELX, VELY, VELZ, ENER",
        "time_integrator":"RK3","solver":"HLLC","reconstruct":"muscl",
        "min_eint":1e-10,"hll_wave_speed":"roe","network_name":"none"}
    values.update({"mode_axis":1} if problem=="SmoothAdvection" else {"deposit_radius":0.12})
    for axis in range(1,4):
        for side in ("l","r"):
            values[f"x{axis}{side}_boundary_type"]="periodic" if problem=="SmoothAdvection" else "outflow"
    template=E/"inputs"/("smooth.par" if problem=="SmoothAdvection" else "sedov.par")
    par=lane/"input.par";par.write_text(comparison.parameters(template.read_text(),values))
    return par
def launch(lane,shape,steps,ranks,profiled=False,baseline=False,common=None,problem="SmoothAdvection",prepared=False,executable=None):
    par=lane/"input.par" if prepared else prepare(lane,shape,steps,problem)
    check=preflight(24 if socket.gethostname()=="gpu-273312" else 16)
    binary=Path(executable) if executable else E/"bin"/("ARCH-baseline" if baseline else "ARCH")
    wrapped=[str(T/"bin/python"),"-B",str(E/"tools/rank_wrapper.py"),str(lane),str(int(profiled)),str(binary),problem,str(par)]
    if baseline:cmd=["taskset","-c","0",*wrapped]
    else:
        base=common or [str(T/"bin/mpiexec"),"--map-by","pe-list="+",".join(map(str,range(ranks)))+":ordered","--bind-to","core","--report-bindings"]
        cmd=[*base,"-n",str(ranks),*wrapped]
    (lane/"launch.json").write_text(json.dumps({"command":cmd,"shape":shape,"steps":steps,"ranks":ranks,"baseline":baseline,"profiled":profiled,"preflight":check},indent=2))
    started=time.monotonic()
    with (lane/"run.log").open("w") as f:
        result=subprocess.run(cmd,stdin=subprocess.DEVNULL,stdout=f,stderr=subprocess.STDOUT,env=ENV,timeout=600)
    elapsed=time.monotonic()-started
    if result.returncode:raise RuntimeError(f"exit {result.returncode}: {lane}")
    final={"command_elapsed_seconds":elapsed,"returncode":result.returncode,"after":preflight(24 if socket.gethostname()=="gpu-273312" else 16)}
    (lane/"completion.json").write_text(json.dumps(final,indent=2))
    return elapsed
def rows(path):
    with path.open() as f:return list(csv.DictReader(f,delimiter="\t"))
def metrics(lane):
    dirs=sorted((lane/"output").glob("rank-*")) or [lane/"output"]
    ranks=[]
    for d in dirs:
        rank=int(d.name.split("-")[-1]) if d.name.startswith("rank-") else 0
        rt=rows(d/"run_timings.tsv")[0]; stages={x["stage"]:float(x["wall_seconds"]) for x in rows(d/"cpu_stage_timings.tsv")}
        p=lane/"metrics"/f"rank-{rank}"
        resources={line.split("\t")[0]:float(line.split("\t")[1]) for line in (p/"resources.tsv").read_text().splitlines()}
        r={"rank":rank,"driver_seconds":float(rt["driver_seconds"]),"output_seconds":float(rt["output_seconds"]),
           "driver_nonoutput_seconds":float(rt["driver_seconds"])-float(rt["output_seconds"]),
           "stages":stages,"resources":resources,"process":json.loads((p/"process.json").read_text())}
        if (p/"profile.json").exists():r["mpi_profile"]=json.loads((p/"profile.json").read_text())
        ranks.append(r)
    return {"ranks":ranks,"critical_driver_nonoutput_seconds":max(r["driver_nonoutput_seconds"] for r in ranks),
            "critical_stage_sum_seconds":max(sum(r["stages"].values()) for r in ranks),
            "sum_rank_peak_rss_kib":sum(r["resources"]["max_rss_kib"] for r in ranks)}
def check(reference,lane):
    return comparison.compare(reference,comparison.snapshots(lane/"output"))
