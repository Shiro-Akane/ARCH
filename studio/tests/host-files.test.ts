import test from 'node:test';import assert from 'node:assert/strict';
import {mkdtemp,mkdir,writeFile,symlink,chmod,rm,truncate,utimes} from 'node:fs/promises';import os from 'node:os';import path from 'node:path';
import {selectedPath,projectRoot,fingerprint} from '../host/files.ts';
test('reject traversal, encoded and absolute OS paths',()=>{for(const p of ['../x','a/../x','/etc/passwd','C:\\secret','a%2f..%2fsecret','a%252fsecret','a\\x','./x'])assert.throws(()=>selectedPath(p));});
test('confined selected files, missing, symlink and permissions',async t=>{
 const dir=await mkdtemp(path.join(os.tmpdir(),'arch-host-'));t.after(()=>rm(dir,{recursive:true,force:true}));const root=path.join(dir,'project');await mkdir(root);assert.equal(await projectRoot(root),root);
 await writeFile(path.join(root,'case.cpp'),'source');const ref=await fingerprint(root,'case.cpp','case-source');assert.equal(ref.exists,true);assert.match(ref.sha256!,/^[a-f0-9]{64}$/);
 assert.equal((await fingerprint(root,'missing.par','parameter')).exists,false);
 await writeFile(path.join(dir,'outside'),'secret');await symlink(path.join(dir,'outside'),path.join(root,'escape'));assert.match((await fingerprint(root,'escape','parameter')).error!,/Symlinks/);
 await chmod(path.join(root,'case.cpp'),0);assert.match((await fingerprint(root,'case.cpp','case-source')).error!,/Permission denied/);await chmod(path.join(root,'case.cpp'),0o600);
 await assert.rejects(projectRoot(path.join(dir,'absent')));
});

test('bounded hashing rejects oversized files and reports a file changing during inspection',async t=>{
 const root=await mkdtemp(path.join(os.tmpdir(),'arch-bounds-'));t.after(()=>rm(root,{recursive:true,force:true}));const file=path.join(root,'binary');await writeFile(file,'');await truncate(file,65*1024*1024);assert.match((await fingerprint(root,'binary','executable')).error!,/limit/);
 await truncate(file,32*1024*1024);let stop=false;let tick=0;
 const writer=(async()=>{while(!stop){await utimes(file,new Date(),new Date(Date.now()+ ++tick*1000));await new Promise(r=>setTimeout(r,1));}})();
 let result;try{result=await fingerprint(root,'binary','executable');}finally{stop=true;await writer;}assert.match(result.error!,/changed during refresh/);
});
