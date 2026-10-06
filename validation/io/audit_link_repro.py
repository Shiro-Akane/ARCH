"""Read-only linker repro archive inventory; never extract or execute its members."""
import argparse,collections,hashlib,json,pathlib,tarfile
def audit(path):
    if path.stat().st_size>64*1024*1024: raise ValueError("Probe archive exceeds 64 MiB budget")
    rows=[];names=collections.defaultdict(list);total=0
    with tarfile.open(path,"r:*") as t:
        for m in t:
            if len(rows)>=256: raise ValueError("Probe archive exceeds 256 member budget")
            if not m.isfile(): raise ValueError("Only regular archive members accepted")
            if m.size<0 or m.size>64*1024*1024: raise ValueError("Member size exceeds probe budget")
            total+=m.size
            if total>64*1024*1024: raise ValueError("Expanded probe archive exceeds 64 MiB budget")
            stream=t.extractfile(m);digest=hashlib.sha256();count=0
            while True:
                data=stream.read(65536)
                if not data:break
                count+=len(data);digest.update(data)
            if count!=m.size:raise ValueError("Truncated archive member")
            row={"name":m.name,"bytes":m.size,"sha256":digest.hexdigest()}
            rows.append(row);names[m.name].append(row)
    duplicate=[{"name":name,"count":len(v),"distinctHashes":len({x["sha256"] for x in v})}
               for name,v in names.items() if len(v)>1]
    return {"status":"INVALID" if duplicate else "UNVERIFIED",
      "scope":"Archive member identity inventory only; unique names do not prove depfile completeness",
      "archiveSha256":hashlib.sha256(path.read_bytes()).hexdigest(),"archiveBytes":path.stat().st_size,
      "members":rows,"duplicateNames":duplicate,
      "canUseAsAuthoritativePathCapture":False,
      "reason":"Duplicate member paths cannot identify actual consumed inputs" if duplicate else "Requires actual depfile-path and lifetime association"}
if __name__=="__main__":
    p=argparse.ArgumentParser(description=__doc__);p.add_argument("archive",type=pathlib.Path);a=p.parse_args()
    result=audit(a.archive);print(json.dumps(result,indent=2));raise SystemExit(1 if result["status"]=="INVALID" else 0)
