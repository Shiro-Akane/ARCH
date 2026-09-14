"""Rebuild the explicitly missing test target, with application identity guard.

The old inherited target list omitted this standalone header-only CUDA test.
Do not reconfigure, modify source, or replace a tested application artifact.
The current scientific sequence will exercise the rebuilt test in its next
declared contract phase. The prior 18-fixture log remains immutable.
"""
import json
import os
from pathlib import Path
import subprocess
import sys

root=Path('/home/ubuntu/projects/ARCH-multiphysics-fix-20260914')
build=root/'build/fix-20260914/release'
out=root/'build/fix-20260914/explicit-amr-test-rebuild-v1'
out.mkdir(parents=True,exist_ok=False)
sys.path.insert(0,str(root/'tools'))
import validation_provenance as provenance
identity=dict(source_root=root,build_dir=build,arch=build/'bin/ARCH',checkpoint_validator=build/'arch_cuda_single_level_validation')
target=build/'arch_cuda_amr_composition'
before=provenance.capture(**identity)
record=dict(status='running',application_identity_before=before,test_before=provenance.file_identity(target))
record['command']=['ninja','-C',str(build),'-v','-j','1','arch_cuda_amr_composition']
(out/'record.json').write_text(json.dumps(record,indent=2)+'\n')
try:
    with (out/'build.log').open('w') as stream:
        rc=subprocess.run(record['command'],cwd=root,stdout=stream,stderr=subprocess.STDOUT,timeout=600).returncode
    record['returncode']=rc
    assert rc==0
    record['test_after']=provenance.file_identity(target)
    assert record['test_after']['sha256']!=record['test_before']['sha256']
    assert b'valid=21 invalid_no_scatter=24' in target.read_bytes()
    record['status']='rebuilt-not-yet-executed'
finally:
    try:
        record['application_identity_after']=provenance.capture(**identity)
        provenance.require_unchanged(before,record['application_identity_after'])
        record['application_identity_unchanged']=True
    except BaseException as error:
        record.update(status='failed',error=repr(error))
        raise
    finally:
        (out/'record.json').write_text(json.dumps(record,indent=2)+'\n')
print('EXPLICIT_AMR_TEST_REBUILT_APPLICATION_IDENTITY_UNCHANGED')
