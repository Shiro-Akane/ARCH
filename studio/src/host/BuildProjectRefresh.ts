import type {BuildSnapshot,ProjectSnapshot} from './contracts.ts';

/** Reconcile only the selected output of a successful managed Build.
 * This refreshes file identities, never reads or saves the Config Working Copy.
 */
export class BuildProjectRefresh {
 private attempted=new Set<string>();
 private errors=new Map<string,Error>();
 async observe(build:BuildSnapshot,project:ProjectSnapshot|null,
  refresh:()=>Promise<ProjectSnapshot>,current:()=>boolean,accept:(project:ProjectSnapshot)=>void){
  const m=build.lastSuccessfulBuild,s=project?.session;
  if(!m||!s||build.activeBuildId||build.state!=='succeeded'
   ||build.latestResult?.state!=='succeeded'||build.latestResult.buildId!==m.buildId
   ||build.projectId!==s.projectId||m.projectId!==s.projectId
   ||m.managedSourceRoot!==s.projectRoot||!s.executable
   ||m.outputBinary.relativePath!==s.executable.relativePath
   ||m.outputBinary.absolutePath!==s.projectRoot+'/'+s.executable.relativePath
   ||m.outputBinary.fingerprint.sha256===s.executable.sha256)return;
  const key=JSON.stringify([s.projectId,m.buildId,m.outputBinary.fingerprint.sha256,s.executable.sha256]);
  if(this.attempted.has(key)){const error=this.errors.get(key);if(error)throw error;return;}
  this.attempted.add(key);
  try{
  const updated=await refresh();
  if(!current())return;
  if(updated.session.projectId!==s.projectId||updated.session.projectRoot!==s.projectRoot
   ||updated.session.executable?.relativePath!==s.executable.relativePath
   ||updated.session.executable.sha256!==m.outputBinary.fingerprint.sha256){
   throw new Error('Build succeeded, but refreshed project binary does not match its manifest. Refresh Project State to inspect the current identity.');
  }
  accept(updated);
  }catch(error){
   if(!current())return;
   const failure=error instanceof Error?error:new Error('Build succeeded, but project identity refresh failed. Refresh Project State.');
   this.errors.set(key,failure);throw failure;
  }
 }
}
