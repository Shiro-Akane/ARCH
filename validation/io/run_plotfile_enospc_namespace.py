#!/usr/bin/env python3
'''Run actual ENOSPC on a bounded, private Linux tmpfs; no science timestep.'''
import argparse,hashlib,json,os,shutil,subprocess,re
from pathlib import Path
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--fixture',required=True,type=Path)
    ap.add_argument('--output-root',required=True,type=Path)
    args=ap.parse_args()
    root=Path(__file__).resolve().parents[2]
    fixture=args.fixture.resolve();out=args.output_root.resolve()
    assert fixture.is_relative_to(root/'studio/.local/integration') and fixture.is_file()
    assert out.is_relative_to(root/'studio/.local/integration') and not out.exists()
    assert os.geteuid()==0,'Use unshare --mount --propagation private as root'
    assert os.readlink('/proc/self/ns/mnt')!=os.readlink('/proc/1/ns/mnt'),'Private mount namespace required'
    out.mkdir();mountpoint=out/'mount';mountpoint.mkdir();mounted=False
    binaries={str(p.relative_to(root)):digest(p) for p in (root/'build-cpu/bin/ARCH',root/'build-cuda/bin/ARCH')}
    summary={'scope':'Real kernel ENOSPC, CPU DriverIO/PlotIO/HDF publication mechanics only; t=0 step=0',
        'fixtureSha256':digest(fixture),'sourceHead':subprocess.check_output(['git','-c','safe.directory='+str(root),'rev-parse','HEAD'],cwd=root,text=True).strip(),
        'sourceDirty':bool(subprocess.check_output(['git','-c','safe.directory='+str(root),'status','--porcelain'],cwd=root,text=True).strip()),
        'tmpfsLimitBytes':4*1024*1024,'privateMountNamespace':True,'productionBinariesBefore':binaries}
    code=1
    try:
        subprocess.run(['mount','-t','tmpfs','-o','size=4M,nosuid,nodev,noexec','arch-plotfile-enospc',str(mountpoint)],check=True)
        mounted=True
        process=subprocess.run([str(fixture),str(mountpoint/'evidence'),'--enospc'],
            env={**os.environ,'OMP_NUM_THREADS':'1','CUDA_VISIBLE_DEVICES':''},capture_output=True,text=True,timeout=30)
        (out/'stdout.log').write_text(process.stdout);(out/'stderr.log').write_text(process.stderr)
        summary['exitCode']=process.returncode
        if (mountpoint/'evidence').exists():
            shutil.copytree(mountpoint/'evidence',out/'raw',ignore=shutil.ignore_patterns('space-reservation'))
            # Preserve private mkstemp permissions while returning these owned
            # evidence copies to the actual project user (not root).
            owner=root.stat()
            for copy in [out/'raw',*(out/'raw').rglob('*')]:
                os.chown(copy,owner.st_uid,owner.st_gid)
        summary['realHdfErrno28']='errno = 28' in process.stderr
        match=re.search(r'PASS actual_kernel_errno=(\d+) tmpfs_bytes=(\d+) reserved_bytes=(\d+) failed_index=(\d+) retry_index=(\d+) original_sha256=([a-f0-9]{64}) time=0 step=0',process.stdout)
        assert process.returncode==0 and match,'Fixture did not complete ENOSPC and same-index retry'
        assert summary['realHdfErrno28'],'Actual HDF error stack lacks kernel errno28'
        errno,total,reserved,failed,retry,sha=match.groups()
        assert int(errno)==28 and int(total)==summary['tmpfsLimitBytes']
        assert int(retry)==int(failed)+1
        summary.update(status='PASS',kernelErrno=int(errno),reservedBytes=int(reserved),failedIndex=int(failed),
            retryNextIndex=int(retry),previousOutputSha256=sha,rawFilesRetainedLocally=True)
        summary['productionBinariesAfter']={p:digest(root/p) for p in binaries}
        assert summary['productionBinariesAfter']==binaries
        code=0
    except Exception as error:
        summary['status']='FAIL';summary['error']=str(error)
        raise
    finally:
        if mounted:
            subprocess.run(['umount',str(mountpoint)],check=True)
        summary['tmpfsUnmounted']=True
        (out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n')
        print(json.dumps(summary))
    return code
if __name__=='__main__':raise SystemExit(main())
