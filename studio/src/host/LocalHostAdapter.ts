import {PROTOCOL_VERSION} from './contracts.ts';
import type {LocalHostAdapter,ProjectSnapshot} from './contracts.ts';
function record(v:unknown):v is Record<string,unknown>{return typeof v==='object'&&v!==null&&!Array.isArray(v);}
function text(v:unknown):v is string{return typeof v==='string'&&v.length>0&&v.length<=8192;}
function date(v:unknown){return text(v)&&Number.isFinite(Date.parse(v));}
export function validateSnapshot(value:unknown):ProjectSnapshot {
 if(!record(value)||!record(value.host)||!record(value.session))throw new Error('Malformed Local Host response');
 const h=value.host,s=value.session;
 if(h.protocolVersion!==PROTOCOL_VERSION)throw new Error('Local Host version is incompatible with this Studio build.');
 if(h.hostKind!=='local'||!text(h.platform)||!text(h.projectRoot)||!record(h.capabilities)||h.capabilities.readProject!==true||typeof h.capabilities.writeConfig!=='boolean'||h.capabilities.build!==false||h.capabilities.preview!==false||h.capabilities.watchFiles!==false)throw new Error('Malformed Local Host capabilities');
 if(!text(s.projectId)||!text(s.displayName)||s.projectRoot!==h.projectRoot||!date(s.openedAt)||!date(s.refreshedAt)||s.mapping!=='unknown'||s.metadata!=='unavailable'||!['missing','available','changed','unknown'].includes(String(s.sourceState))||!['missing','available','changed-externally','unknown'].includes(String(s.configFileState))||!['missing','available','unknown'].includes(String(s.binaryState)))throw new Error('Malformed Project Session');
 for(const [key,kind] of [['caseSource','case-source'],['parameterFile','parameter'],['executable','executable']]) {
  const f=s[key];if(f===undefined)continue;
  if(!record(f)||!text(f.relativePath)||f.relativePath.startsWith('/')||/[\\%:]/.test(f.relativePath)||f.relativePath.split('/').some(x=>!x||x==='.'||x==='..')||f.kind!==kind||typeof f.exists!=='boolean'||typeof f.changed!=='boolean'||(f.error!==undefined&&!text(f.error)))throw new Error('Malformed selected file identity');
  if(f.exists&&(!Number.isSafeInteger(f.size)||Number(f.size)<0||Number(f.size)>64*1024*1024||!date(f.modifiedTime)||typeof f.sha256!=='string'||!/^[a-f0-9]{64}$/.test(f.sha256)))throw new Error('Malformed selected file fingerprint');
 }
 return value as unknown as ProjectSnapshot;
}
export class HttpLocalHostAdapter implements LocalHostAdapter {
 private base:string;
 private transport:typeof fetch;
 constructor(base='http://127.0.0.1:4180',transport:typeof fetch=(...args)=>fetch(...args)){const url=new URL(base);if(url.protocol!=='http:'||url.hostname!=='127.0.0.1'||url.username||url.password||url.search||url.hash||url.pathname!=='/')throw new Error('Local Host URL must be a loopback HTTP origin');this.base=url.origin;this.transport=transport;}
 private async request(refresh:boolean):Promise<ProjectSnapshot>{
  let response:Response;
  try{response=await this.transport(this.base+(refresh?'/api/project/refresh':'/api/project'),{method:refresh?'POST':'GET',headers:{'X-ARCH-Studio':'1','X-ARCH-Protocol':PROTOCOL_VERSION},credentials:'omit',redirect:'error',signal:AbortSignal.timeout(10000)});}catch{throw new Error('Local Host unavailable. Start the local service and check its authorized UI origin.');}
  if(!response.ok)throw new Error(`Local Host request failed (${response.status}); previous session retained.`);
  if(!response.headers.get('content-type')?.includes('application/json'))throw new Error('Malformed Local Host response');
  const reader=response.body?.getReader();if(!reader)throw new Error('Empty Local Host response');let bytes=0;const chunks:Uint8Array[]=[];
  try{while(true){const {done,value}=await reader.read();if(done)break;bytes+=value.length;if(bytes>65536){await reader.cancel();throw new Error('Local Host response exceeds limit');}chunks.push(value);}}finally{reader.releaseLock();}
  const all=new Uint8Array(bytes);let offset=0;for(const chunk of chunks){all.set(chunk,offset);offset+=chunk.length;}
  let data:unknown;try{data=JSON.parse(new TextDecoder('utf-8',{fatal:true}).decode(all));}catch{throw new Error('Malformed Local Host response');}return validateSnapshot(data);
 }
 connect(){return this.request(false);}
 refresh(){return this.request(true);}
}
