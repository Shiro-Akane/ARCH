import json,os,shlex,subprocess,sys
from pathlib import Path
os.environ["ARCH_MPI_EVIDENCE"]="/home/ubuntu/projects/ARCH-mpi-evidence-optimization-20261101"
from measure import *
C=Path("/home/ubuntu/preservation/arch-mpi-optimization-20261101")
ssh=["ssh","-i",str(C/"temporary_transport_key"),"-o","IdentitiesOnly=yes","-o","BatchMode=yes","-o","StrictHostKeyChecking=yes","-o","UserKnownHostsFile="+str(C/"known_hosts")]
transfer_env=os.environ.copy();transfer_env.pop("LD_LIBRARY_PATH",None)
def transfer(args):
    subprocess.run(["rsync","-a","-e",shlex.join(ssh),*args],stdin=subprocess.DEVNULL,env=transfer_env,check=True,timeout=60)
common=json.loads((E/"multihost-launch.json").read_text())
report={"purpose":"two-host correctness only; shared GPU host is not a timing baseline","cases":[],"host_tests":[]}
for ranks in (2,4):
    base=common.copy();base[base.index("--map-by")+1]=f"ppr:{ranks//2}:node"
    log=E/f"cross-host-unit-{ranks}.log"
    with log.open("w") as f:
        r=subprocess.run([*base,"-n",str(ranks),str(E/"bin/mpi_fixed_grid-after")],stdin=subprocess.DEVNULL,stdout=f,stderr=subprocess.STDOUT,env=ENV,timeout=60)
    if r.returncode:raise RuntimeError(str(log))
    report["host_tests"].append({"ranks":ranks,"passed":True})
for name,shape,problem in (("smooth2d",(5,3,0),"SmoothAdvection"),("sedov3d",(5,3,2),"Sedov")):
    root=E/"cross-optimized"/name;root.mkdir(parents=True,exist_ok=True)
    lane=root/"baseline";launch(lane,shape,8,0,baseline=True,problem=problem)
    reference=comparison.snapshots(lane/"output")
    for ranks in (2,4):
        lane=root/f"mpi{ranks}";prepare(lane,shape,8,problem)
        transfer([str(E/"cross-optimized"),"ubuntu@150.65.181.189:"+str(E)+"/"])
        base=common.copy();base[base.index("--map-by")+1]=f"ppr:{ranks//2}:node"
        launch(lane,shape,8,ranks,True,common=base,problem=problem,prepared=True,executable=E/"bin/ARCH-after")
        transfer(["ubuntu@150.65.181.189:"+str(lane)+"/",str(lane)+"/"])
        cmp=check(reference,lane); met=metrics(lane)
        hosts=sorted(set(r["process"]["host"] for r in met["ranks"]))
        assert hosts==["gen-989544","gpu-273312"],hosts
        report["cases"].append({"name":name,"shape":shape,"steps":8,"ranks":ranks,"hosts":hosts,"comparison":cmp,"measurements":met})
        (E/"cross-summary.json").write_text(json.dumps(report,indent=2))
        print(name,ranks,"bitwise equal",cmp["cells"],hosts,flush=True)
report["complete"]=True
(E/"cross-summary.json").write_text(json.dumps(report,indent=2))
