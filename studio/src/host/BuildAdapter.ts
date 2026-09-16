import {PROTOCOL_VERSION} from './contracts.ts';
import type {BuildSnapshot,BuildEvents,SourceResponse} from './contracts.ts';
export async function buildRequest(route:string,body?:unknown):Promise<unknown>{
 const response=await fetch('http://127.0.0.1:4180'+route,{method:body?'POST':'GET',headers:{'X-ARCH-Studio':'1','X-ARCH-Protocol':PROTOCOL_VERSION,...(body?{'Content-Type':'application/json'}:{})},body:body?JSON.stringify(body):undefined,credentials:'omit',redirect:'error',signal:AbortSignal.timeout(15000)});
 const reader=response.body?.getReader();if(!reader)throw new Error('Empty Build response');const chunks:Uint8Array[]=[];let size=0;
 try{while(true){const {value,done}=await reader.read();if(done)break;size+=value.length;if(size>2*1024*1024){await reader.cancel();throw new Error('Build response exceeds limit');}chunks.push(value);}}finally{reader.releaseLock();}
 const bytes=new Uint8Array(size);let offset=0;for(const c of chunks){bytes.set(c,offset);offset+=c.length;}const data=JSON.parse(new TextDecoder('utf8',{fatal:true}).decode(bytes));
 if(!response.ok)throw new Error(data?.error?.message??`Build request failed (${response.status})`);return data;
}
function record(v:unknown):v is Record<string,unknown>{return !!v&&typeof v==='object'&&!Array.isArray(v);}
export function validateBuildSnapshot(v:unknown,projectId:string):BuildSnapshot{
 if(!record(v)||v.protocolVersion!==PROTOCOL_VERSION||v.projectId!==projectId||typeof v.configured!=='boolean'||!['not-configured','ready','queued','building','succeeded','failed','cancelled','unknown'].includes(String(v.state))||!['configured','unknown'].includes(String(v.mappingState))||!['missing','available','built-from-current-tracked-inputs','needs-build','freshness-unknown'].includes(String(v.binaryState))||typeof v.freshnessReason!=='string'||!Array.isArray(v.changedInputs)||v.changedInputs.some(x=>typeof x!=='string'))throw new Error('Incompatible or stale Build response');
 if(v.configured&&(!record(v.profile)||typeof v.profile.id!=='string'||typeof v.profile.managedSourceRoot!=='string'||typeof v.profile.buildDirRelative!=='string'||typeof v.profile.outputBinaryRelative!=='string'||typeof v.profile.target!=='string'||!Array.isArray(v.profile.trackedInputs)))throw new Error('Malformed Build Profile');
 if(v.lastSuccessfulBuild!==undefined){const m=v.lastSuccessfulBuild;if(!record(m)||m.manifestVersion!=='1'||typeof m.buildId!=='string'||typeof m.managedSourceRoot!=='string'||typeof m.finishedAt!=='string'||typeof m.buildProfileFingerprint!=='string'||!record(m.outputBinary)||!record(m.outputBinary.fingerprint)||typeof m.outputBinary.fingerprint.sha256!=='string'||!Array.isArray(m.trackedInputFingerprints))throw new Error('Malformed Build manifest');}
 if(v.latestResult!==undefined){const result=v.latestResult;if(!record(result)||result.projectId!==projectId||typeof result.buildId!=='string'||!['succeeded','failed','cancelled'].includes(String(result.state))||(result.error!==undefined&&typeof result.error!=='string'))throw new Error('Malformed Build result');}
 if(v.activeBuildId!==undefined&&(typeof v.activeBuildId!=='string'||!/^[-a-f0-9]{36}$/.test(v.activeBuildId)))throw new Error('Malformed build ID');
 return v as unknown as BuildSnapshot;
}
export function validateBuildEvents(v:unknown,projectId:string,buildId:string):BuildEvents{
 if(!record(v)||v.protocolVersion!==PROTOCOL_VERSION||v.projectId!==projectId||v.buildId!==buildId||!Array.isArray(v.events)||v.events.length>1024||typeof v.truncated!=='boolean'||!Number.isSafeInteger(v.lastSequence))throw new Error('Incompatible or stale Build events');
 let last=0,total=0;for(const e of v.events){if(!record(e)||e.projectId!==projectId||e.buildId!==buildId||!Number.isSafeInteger(e.sequence)||Number(e.sequence)<=last||!['state','stdout','stderr'].includes(String(e.kind))||(e.text!==undefined&&(typeof e.text!=='string'||e.text.length>4096)))throw new Error('Malformed Build event');last=Number(e.sequence);total+=typeof e.text==='string'?e.text.length:0;}if(total>256*1024||last>Number(v.lastSequence))throw new Error('Build events exceed limit');return v as unknown as BuildEvents;
}
export function validateSource(v:unknown,projectId:string):SourceResponse{if(!record(v)||v.protocolVersion!==PROTOCOL_VERSION||v.projectId!==projectId||typeof v.relativePath!=='string'||typeof v.text!=='string'||v.text.length>256*1024)throw new Error('Malformed source response');return v as unknown as SourceResponse;}
