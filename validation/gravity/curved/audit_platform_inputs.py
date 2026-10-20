"""Read-only declared-input audit before owner-frozen benchmark selection.
No Setup/EOS loading/CUDA initialization/simulation or scientific readiness claim.
"""
import argparse,hashlib,json,subprocess
from pathlib import Path

def digest(p):
    h=hashlib.sha256()
    with p.open("rb") as stream:
        for block in iter(lambda:stream.read(1024*1024),b""):h.update(block)
    return h.hexdigest()

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--arch",required=True,type=Path)
    p.add_argument("--source-root",required=True,type=Path)
    p.add_argument("--case",required=True)
    p.add_argument("--input",required=True,action="append",type=Path)
    p.add_argument("--output",required=True,type=Path)
    a=p.parse_args();root=a.source_root.resolve();binary=a.arch.resolve()
    if a.output.exists():p.error("output must be new")
    inputs=[v.resolve(strict=True) for v in a.input]
    binary_sha=digest(binary)
    a.output.mkdir(parents=True,exist_ok=False)
    rows=[]
    for i,source in enumerate(inputs):
        raw=source.read_bytes();request="platform-audit-"+str(i)
        q=subprocess.run([str(binary),"--inspect-config",a.case,"--config-stdin","--request-id",request],
            input=raw,cwd=root,capture_output=True,timeout=30)
        (a.output/(str(i)+".stdout.json")).write_bytes(q.stdout)
        (a.output/(str(i)+".stderr.txt")).write_bytes(q.stderr)
        response=json.loads(q.stdout)
        if q.returncode not in (0,3):raise RuntimeError("unexpected inspect exit code")
        identity=response["identity"]
        if identity["caseId"]!=a.case or identity["requestId"]!=request or identity["configRevision"]!=hashlib.sha256(raw).hexdigest():
            raise RuntimeError("response identity mismatch")
        execution=response["execution"]
        for key,expected in {"setup":"not_executed","eos":"not_loaded","cuda":"not_initialized","filesystem":"not_accessed","simulationReadiness":"not_checked"}.items():
            if execution[key]!=expected:raise RuntimeError("inspection execution scope changed")
        paths=[]
        for parameter in response["parameters"]:
            contract=parameter.get("path")
            value=parameter.get("parsedValue")
            if not contract or contract.get("role")!="input-file" or not isinstance(value,str) or not value:
                continue
            target=Path(value);target=target if target.is_absolute() else root/target
            item=dict(key=parameter["key"],rawPath=value,workingDirectory=str(root),
                resolvedPath=str(target.resolve()),isFile=target.is_file(),
                scope="host read-only declared input path; not authoritative model selection")
            if item["isFile"]:
                item.update(sizeBytes=target.stat().st_size,sha256=digest(target))
            paths.append(item)
        rows.append(dict(inputPath=str(source),inputSha256=hashlib.sha256(raw).hexdigest(),
            exitCode=q.returncode,status=response["status"],completeness=response["completeness"],
            coordinates=response["coordinates"],diagnostics=response["diagnostics"],
            execution=execution,declaredInputPaths=paths))
    if digest(binary)!=binary_sha:raise RuntimeError("binary changed during audit")
    result=dict(status="READ_ONLY_AUDIT_COMPLETE_NOT_SCIENTIFIC_ACCEPTANCE",
        binarySha256=binary_sha,caseId=a.case,rows=rows,
        limitations=["Declared configuration only, no actual Setup/resources/evolution",
            "Path hash does not prove EOS model selection or scientific identity acceptance",
            "No owner-frozen endpoint/budget inferred from tmax or step smoke inputs",
            "Legacy polar and new RZ are separate semantic identities"])
    (a.output/"summary.json").write_text(json.dumps(result,indent=2)+"\n")
    print("PLATFORM_INPUT_AUDIT",len(rows),"declaration-complete",sum(v["exitCode"]==0 for v in rows))
if __name__=="__main__":main()
