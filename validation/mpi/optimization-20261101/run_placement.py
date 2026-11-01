import json,os,sys
from pathlib import Path
E=Path("/home/ubuntu/projects/ARCH-mpi-evidence-optimization-20261101")
os.environ["ARCH_MPI_EVIDENCE"]=str(E)
sys.path.insert(0,str(E/"tools"))
from measure import *
result={"purpose":"test whether MPI waiting follows rank data or CPU placement","cases":[]}
base=E/"placement"/"baseline"
launch(base,(16,8,0),64,0,baseline=True)
reference=comparison.snapshots(base/"output")
for repetition in range(3):
 for name,cores in (("normal",[0,1,2,3]),("reverse",[3,2,1,0]),("rotate",[1,2,3,0])):
  lane=E/"placement"/f"{name}-{repetition}"
  common=[str(T/"bin/mpiexec"),"--map-by","pe-list="+",".join(map(str,cores))+":ordered","--bind-to","core","--report-bindings"]
  launch(lane,(16,8,0),64,4,True,common=common)
  result["cases"].append({"layout":name,"repeat":repetition,"comparison":check(reference,lane),"metrics":metrics(lane)})
  (E/"diagnosis-summary.json").write_text(json.dumps(result,indent=2))
  print(name,repetition,"passed",flush=True)
result["complete"]=True
(E/"diagnosis-summary.json").write_text(json.dumps(result,indent=2))
