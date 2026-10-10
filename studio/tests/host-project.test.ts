import test from 'node:test';import assert from 'node:assert/strict';import {mkdtemp,writeFile,readFile,rm,chmod,mkdir} from 'node:fs/promises';import path from 'node:path';import os from 'node:os';import {openProject} from '../host/project.ts';
import {createHash} from 'node:crypto';
import {BuildError} from '../host/buildRunner.ts';
import {createHostServer,listenLocal} from '../host/server.ts';
import {PROTOCOL_VERSION} from '../src/host/contracts.ts';
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

test('actual selected-source owner returns typed 409 for wrong model, edited source and missing registration',async t=>{
 const root=await mkdtemp(path.join(os.tmpdir(),'arch-source-owner-'));t.after(()=>rm(root,{recursive:true,force:true}));
 const source='simulation/Sod/Sod.cpp',sourceText='frozen selected source';await mkdir(path.dirname(root+'/'+source),{recursive:true});await writeFile(root+'/'+source,sourceText);
 const registry=JSON.parse(await readFile(new URL('../../src/api/examples/local-workflow/registered-cases.json',import.meta.url),'utf8'));
 const entry=registry.cases.find((c:{caseId:string})=>c.caseId==='Sod');entry.inspection.sourceFile=root+'/'+source;entry.inspection.compiledSourceSha256=createHash('sha256').update(sourceText).digest('hex');
 await writeFile(root+'/registry.json',JSON.stringify(registry));
 await writeFile(root+'/ARCH','#!'+process.execPath+'\nconst fs=require("node:fs");if(process.argv[2]==="--list-cases")process.stdout.write(fs.readFileSync("registry.json"));else process.exit(99);\n',{mode:0o755});
 const owner=await openProject({project:root,case:source,binary:'ARCH',selectedSource:source,requestedCaseId:'Sod'});
 await owner.configuration!.assertCase!('Sod');
 await assert.rejects(owner.configuration!.assertCase!('CellularDet'),error=>{assert.ok(error instanceof BuildError);assert.equal(error.status,409);assert.match(error.message,/Close this project and reopen/);return true;});
 const origin='http://127.0.0.1:4179',server=createHostServer(owner,origin);await listenLocal(server,0);t.after(()=>new Promise<void>(resolve=>server.close(()=>resolve())));
 const url='http://127.0.0.1:'+(server.address() as {port:number}).port+'/api/configuration/inspect';
 const configText='nblockx1=8\n',configRevision=createHash('sha256').update(configText).digest('hex');
 const inspect=async(caseId:string)=>{
  const response=await fetch(url,{method:'POST',headers:{Origin:origin,'X-ARCH-Studio':'1','X-ARCH-Protocol':PROTOCOL_VERSION,'Content-Type':'application/json'},body:JSON.stringify({projectId:owner.snapshot().session.projectId,caseId,configText,configRevision})});
  assert.equal(response.status,409);const body=await response.json();assert.equal(body.error.code,'build-error');return body.error.message as string;
 };
 assert.match(await inspect('CellularDet'),/Close this project and reopen/);
 await writeFile(root+'/'+source,'edited selected source');assert.match(await inspect('Sod'),/differs from its compiled registration/);
 await writeFile(root+'/'+source,sourceText);await writeFile(root+'/registry.json',JSON.stringify({...registry,cases:registry.cases.filter((c:{caseId:string})=>c.caseId!=='Sod')}));
 assert.match(await inspect('Sod'),/no unique authoritative compiled case association/);
 const unbound=await openProject({project:root,case:source,binary:'ARCH'});assert.equal(unbound.configuration!.assertCase,undefined);
});
