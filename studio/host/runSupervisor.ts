/**
 * Validate live run ownership using boot identity and process start ticks.
 * The /proc record is checked in addition to the PID so reuse or a stale saved
 * record cannot authorize Stop. Unavailable identity leaves the outcome unknown.
 */
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
  // /proc/PID/stat starts with PID and a parenthesized command that can contain
  // spaces. After its final ')', offset 0 is field 3 (state), and offset 19 is
  // field 22 (starttime in clock ticks); the PID alone is not stable identity.
  const fields=raw.slice(raw.lastIndexOf(')')+2).split(' ');
  if(fields[0]==='Z'||fields[19]!==state.workerStartTicks)throw unavailable();
 }catch{throw unavailable();}
}
