import test from 'node:test';
import assert from 'node:assert/strict';
import path from 'node:path';
import {tmpdir} from 'node:os';
import {mkdtemp,mkdir,writeFile,readFile,rm} from 'node:fs/promises';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {openDesktopProject} from '../host/desktopProject.ts';
import {registeredSourceCase} from '../host/desktopSource.ts';
import {configurationRelativePath} from '../desktop/arguments.mjs';

const windows=process.platform==='win32';
const env=process.env;
const tools={cmake:env.ARCH_TEST_CMAKE!,supervisor:env.ARCH_TEST_SUPERVISOR!,path:env.PATH!,ninja:env.ARCH_TEST_NINJA!,objdump:env.ARCH_TEST_OBJDUMP!,publisher:env.ARCH_TEST_PUBLISHER!,rc:env.ARCH_TEST_RC!};
const native=windows&&Object.values(tools).every(Boolean);
test('native source association normalizes drive and separators without relaxing directory case',{skip:!windows},()=>{
 const root='C:\\Project',source='simulation/Sod/Sod.cpp';
 const cases=[{caseId:'Sod',inspection:{sourceFile:'c:/Project/simulation/Sod/Sod.cpp'}}];
 assert.equal(registeredSourceCase(cases,root,source),'Sod');
 assert.throws(()=>registeredSourceCase(cases,root,'../other/Sod.cpp'),/outside/);
 assert.throws(()=>registeredSourceCase(cases,root,'..'),/outside/);
 assert.throws(()=>registeredSourceCase(cases,root,'simulation/sod/Sod.cpp'),/unique/);
 assert.throws(()=>registeredSourceCase([...cases,...cases],root,source),/unique/);
});
test('native desktop requires a trusted private tool context',{skip:!windows},async()=>{
 await assert.rejects(openDesktopProject({cwd:process.cwd()}),/Trusted Windows desktop tools/);
 const launch={cwd:process.cwd(),origin:'http://127.0.0.1:4190',token:'a'.repeat(64)};
 for(const windowsTools of [undefined,{...tools,program:'anything'}]){
  const child=spawnSync(process.execPath,[fileURLToPath(new URL('../host/desktop.ts',import.meta.url)),Buffer.from(JSON.stringify({...launch,windowsTools})).toString('base64url')],{encoding:'utf8',windowsHide:true,timeout:10000});
  assert.equal(child.status,1,child.stderr);
  assert.match(child.stderr,windowsTools?/Invalid private Windows/:/Trusted Windows desktop tools/);
 }
});
test('native unconfigured source keeps missing Build gate and one private tool context',{skip:!native},async t=>{
 const root=await mkdtemp(path.join(tmpdir(),'ARCH native desktop '));t.after(()=>rm(root,{recursive:true,force:true}));
 await mkdir(path.join(root,'simulation','NewCase'),{recursive:true});
 await writeFile(path.join(root,'CMakeLists.txt'),'project(ARCH)\n');await writeFile(path.join(root,'CMakePresets.json'),'{}');
 await writeFile(path.join(root,'simulation','NewCase','NewCase.cpp'),'// pending source fixture\n');
 const opened=await openDesktopProject({project:root,cwd:root,source:'simulation/NewCase/NewCase.cpp'},{windowsTools:tools});
 t.after(async()=>{await opened.reader.configure?.shutdown();await opened.reader.preview?.shutdown();});
 assert.equal(opened.registrationPending,true);assert.equal(opened.reader.snapshot().session.binaryState,'missing');
 assert.equal(opened.reader.build?.profile.outputBinaryRelative,'build-studio-cpu/bin/ARCH.exe');
 assert.equal(opened.reader.build?.profile.observedInputScope,'observed-build-inputs-v1');
 assert.equal(opened.reader.build?.profile.dependenciesComplete,false);
 assert.deepEqual(opened.reader.nativeTools(),tools);assert.deepEqual(opened.reader.configure?.profile.windowsTools,tools);
 assert.equal(JSON.stringify(opened.reader.snapshot()).includes('windowsTools'),false);
 assert.equal((await opened.reader.preview!.readiness()).ready,false);
 await assert.rejects(opened.reader.configuration!.discovery(),/missing|unavailable|ENOENT/i);
});
test('native dialog paths reach unchanged Open/Save As owners and preserve exact bytes',{skip:!native},async t=>{
 const root=await mkdtemp(path.join(tmpdir(),'ARCH native dialog '));t.after(()=>rm(root,{recursive:true,force:true}));
 await mkdir(path.join(root,'simulation','Sod'),{recursive:true});await mkdir(path.join(root,'copies'));
 await writeFile(path.join(root,'CMakeLists.txt'),'project(ARCH)\n');await writeFile(path.join(root,'CMakePresets.json'),'{}');
 const original=path.join(root,'simulation','Sod','Sod.par'),text='\ufeff# dialog fixture\r\nx = 8\r\n';await writeFile(original,text);
 const opened=await openDesktopProject({project:root,cwd:root},{windowsTools:tools});
 t.after(async()=>{await opened.reader.configure?.shutdown();await opened.reader.preview?.shutdown();});
 const projectId=opened.reader.snapshot().session.projectId;
 const read=await opened.reader.openConfig({projectId,relativePath:configurationRelativePath(root,original,'win32')});assert.equal(read.text,text);
 const destination=path.join(root,'copies','原生 copy.par'),relative=configurationRelativePath(root,destination,'win32');
 const saved=await opened.reader.saveConfigAs({projectId,destinationRelativePath:relative,text:read.text});assert.equal(saved.relativePath,'copies/原生 copy.par');
 assert.equal(await readFile(destination,'utf8'),text);assert.equal(await readFile(original,'utf8'),text);
 assert.equal((await opened.reader.openConfig({projectId,relativePath:relative})).text,text);
});
