import test from 'node:test';
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {verifyRunSupervisor} from '../host/runSupervisor.ts';
import type {RunState} from '../src/host/runContracts.ts';
test('live supervisor requires matching boot and process start identity; historical completion remains readable',async()=>{
 const raw=await readFile('/proc/self/stat','utf8');
 const state:RunState={runId:'identity-test',state:'running',workerPid:process.pid,
  workerStartTicks:raw.slice(raw.lastIndexOf(')')+2).split(' ')[19],
  bootId:(await readFile('/proc/sys/kernel/random/boot_id','utf8')).trim(),startedAt:new Date().toISOString()};
 await verifyRunSupervisor(state);
 for(const change of [{bootId:'another-boot'},{workerStartTicks:'0'},{workerStartTicks:undefined},{workerPid:2147483647}]){
  await assert.rejects(verifyRunSupervisor({...state,...change}),/outcome is unknown/);
 }
 await verifyRunSupervisor({...state,state:'succeeded',workerPid:2147483647});
});
