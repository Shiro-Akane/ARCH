import {readFile} from 'node:fs/promises';
import type {RunState} from '../src/host/runContracts.ts';
import {BuildError} from './buildRunner.ts';
/** A saved running record is evidence of a past state, not a live process. */
export async function verifyRunSupervisor(state:RunState){
 if(state.state!=='starting'&&state.state!=='running')return;
 const unavailable=()=>new BuildError('Run supervisor identity unavailable or no longer alive; computation outcome is unknown. Inspect the retained terminal and run log before retrying.',409);
 if(!state.workerStartTicks||!state.bootId)throw unavailable();
 try{
  if((await readFile('/proc/sys/kernel/random/boot_id','utf8')).trim()!==state.bootId)throw unavailable();
  const raw=await readFile('/proc/'+state.workerPid+'/stat','utf8');
  const fields=raw.slice(raw.lastIndexOf(')')+2).split(' ');
  if(fields[0]==='Z'||fields[19]!==state.workerStartTicks)throw unavailable();
 }catch{throw unavailable();}
}
