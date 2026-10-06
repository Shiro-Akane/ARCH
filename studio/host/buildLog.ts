import type {BuildEvent,BuildEvents,BuildState} from '../src/host/contracts.ts';
import {PROTOCOL_VERSION} from '../src/host/contracts.ts';
import {stripVTControlCharacters} from 'node:util';
export class BuildLog {
 private events:BuildEvent[]=[];private bytes=0;private sequence=0;private truncated=false;
 readonly projectId:string;readonly buildId:string;
 constructor(projectId:string,buildId:string){this.projectId=projectId;this.buildId=buildId;}
 append(kind:BuildEvent['kind'],text?:string,state?:BuildState){
  const clean=text===undefined?'':Array.from(stripVTControlCharacters(text)).filter(c=>c==='\n'||c==='\t'||(c.charCodeAt(0)>=32&&c.charCodeAt(0)!==127)).join('');
  for(let offset=0;offset<Math.max(1,clean.length);offset+=4096){
   const chunk=clean.slice(offset,offset+4096);this.events.push({projectId:this.projectId,buildId:this.buildId,sequence:++this.sequence,timestamp:new Date().toISOString(),kind,...(text!==undefined?{text:chunk}:{}),...(state?{state}:{})});this.bytes+=chunk.length;
   while(this.events.length>1024||this.bytes>256*1024){this.bytes-=this.events.shift()?.text?.length??0;this.truncated=true;}
  }
 }
 snapshot():BuildEvents{return {protocolVersion:PROTOCOL_VERSION,projectId:this.projectId,buildId:this.buildId,events:structuredClone(this.events),truncated:this.truncated,lastSequence:this.sequence};}
}
