import os,sys,json
from pathlib import Path
E=Path("/home/ubuntu/projects/ARCH-mpi-evidence-optimization-20261101")
os.environ["ARCH_MPI_EVIDENCE"]=str(E)
os.environ["ARCH_MPI_PROFILE_LIBRARY"]=str(E/"bin/libmpi_trace.so")
sys.path.insert(0,str(E/"tools"))
from measure import *
reference=comparison.snapshots(E/"paired/smooth2d/baseline/output")
result={"cases":[],"complete":False}
for repeat in range(2):
 for version in ("before","after"):
  lane=E/"aligned-trace"/f"repeat{repeat}-{version}"
  launch(lane,(16,8,0),64,4,True,executable=E/"bin"/("ARCH" if version=="before" else "ARCH-after"))
  m=metrics(lane)
  assert all("trace" in x["mpi_profile"] for x in m["ranks"])
  result["cases"].append({"repeat":repeat,"version":version,"comparison":check(reference,lane),"metrics":m})
  (E/"aligned-trace-summary.json").write_text(json.dumps(result,indent=2))
  print(repeat,version,"passed",flush=True)
result["complete"]=True
(E/"aligned-trace-summary.json").write_text(json.dumps(result,indent=2))
