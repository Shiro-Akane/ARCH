import test from 'node:test';import assert from 'node:assert/strict';import {mkdtemp,writeFile,rm,chmod} from 'node:fs/promises';import path from 'node:path';import os from 'node:os';import {openProject} from '../host/project.ts';
test('project identity separates missing, available and unknown without guessing binary mapping',async t=>{
 const root=await mkdtemp(path.join(os.tmpdir(),'arch-project-'));t.after(()=>rm(root,{recursive:true,force:true}));await writeFile(path.join(root,'source.cpp'),'not parsed');
 const project=await openProject({project:root,case:'source.cpp',config:'absent.par'});const {host,session}=project.snapshot();assert.equal(session.sourceState,'available');assert.equal(session.configFileState,'missing');assert.equal(session.binaryState,'unknown');assert.equal(session.mapping,'unknown');assert.equal(session.metadata,'unavailable');assert.equal(host.capabilities.build,false);assert.equal(host.capabilities.writeConfig,true);
 session.sourceState='missing';assert.equal(project.snapshot().session.sourceState,'available');
});

test('manual refresh compares initial fingerprints, reports binary changes without stale inference and preserves failed objects',async t=>{
 const root=await mkdtemp(path.join(os.tmpdir(),'arch-refresh-'));t.after(()=>rm(root,{recursive:true,force:true}));for(const name of ['source.cpp','config.par','binary'])await writeFile(path.join(root,name),'original');
 const project=await openProject({project:root,case:'source.cpp',config:'config.par',binary:'binary'});const initial=project.snapshot();
 for(const name of ['source.cpp','config.par','binary'])await writeFile(path.join(root,name),'external edit');
 const updated=await project.refresh();assert.equal(updated.session.sourceState,'changed');assert.equal(updated.session.configFileState,'changed-externally');assert.equal(updated.session.binaryState,'available');assert.equal(updated.session.executable?.changed,true);assert.equal(updated.session.projectId,initial.session.projectId);
 assert.equal((await project.refresh()).session.sourceState,'changed');
 await rm(path.join(root,'config.par'));assert.equal((await project.refresh()).session.configFileState,'missing');
 await rm(root,{recursive:true});await assert.rejects(project.refresh());assert.equal(project.snapshot().session.caseSource?.sha256,updated.session.caseSource?.sha256);
});

test('failed selected-file refresh retains its fingerprint then recovers',async t=>{
 const root=await mkdtemp(path.join(os.tmpdir(),'arch-recover-'));t.after(()=>rm(root,{recursive:true,force:true}));const file=path.join(root,'config.par');await writeFile(file,'x = 1');const project=await openProject({project:root,config:'config.par'});const before=project.snapshot();
 await chmod(file,0);const failed=await project.refresh();assert.equal(failed.session.configFileState,'unknown');assert.equal(failed.session.parameterFile?.sha256,before.session.parameterFile?.sha256);assert.match(failed.session.parameterFile!.error!,/Permission denied/);
 await chmod(file,0o600);await writeFile(file,'x = 2');const recovered=await project.refresh();assert.equal(recovered.session.configFileState,'changed-externally');assert.equal(recovered.session.parameterFile?.error,undefined);
});
