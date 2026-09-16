import {BuildError} from './buildRunner.ts';
import type {BuildRunner} from './buildRunner.ts';
import {PROTOCOL_VERSION} from '../src/host/contracts.ts';
import type {ConfigReadResponse,SaveConfigRequest,SaveConfigAsRequest,ConfigWriteResponse} from '../src/host/contracts.ts';
import {ConfigError} from './config.ts';
import { createServer } from 'node:http';
import type { Server } from 'node:http';
import type { ProjectSnapshot } from '../src/host/contracts.ts';
export interface ProjectReader { readSource?():Promise<unknown>; build?:BuildRunner; saveConfig?(request:SaveConfigRequest):Promise<ConfigWriteResponse>; saveConfigAs?(request:SaveConfigAsRequest):Promise<ConfigWriteResponse>; readConfig?(): Promise<ConfigReadResponse>; snapshot(): ProjectSnapshot; refresh(): Promise<ProjectSnapshot> }
export function createHostServer(reader: ProjectReader, origin: string): Server {
  const allowed = new URL(origin);
  if (allowed.protocol !== 'http:' || allowed.hostname !== '127.0.0.1' || allowed.origin !== origin) throw new Error('UI origin must be an exact http://127.0.0.1:PORT origin');
  return createServer(async (req, res) => {
    const send = (status: number, value: unknown) => {res.writeHead(status, {'Content-Type':'application/json', 'Cache-Control':'no-store', 'X-Content-Type-Options':'nosniff'});res.end(JSON.stringify(value));};
    const address = req.socket.localPort;
    if (req.headers.host !== `127.0.0.1:${address}` || req.headers.origin !== origin) {send(403,{error:'Host or Origin rejected'});return;}
    res.setHeader('Access-Control-Allow-Origin',origin);
    res.setHeader('Vary','Origin');
    const eventMatch=/^\/api\/build\/([a-f0-9-]{36})\/events$/.exec(req.url??'');
    const routes = ['/api/source','/api/build','/api/build/profile','/api/build/status','/api/config/save','/api/config/save-as','/api/config','/api/health','/api/host','/api/project','/api/project/files','/api/project/refresh'];
    if (!routes.includes(req.url ?? '')&&!eventMatch) {send(404,{error:'Unknown endpoint'});return;}
    if (req.method === 'OPTIONS') {res.setHeader('Access-Control-Allow-Methods','GET, POST');res.setHeader('Access-Control-Allow-Headers','X-ARCH-Studio, X-ARCH-Protocol, Content-Type');send(200,{});return;}
    if (req.headers['x-arch-studio'] !== '1') {send(403,{error:'Studio request header required'});return;}
    if(req.headers['x-arch-protocol']!==PROTOCOL_VERSION){send(426,{error:{code:'protocol-error',message:'Local Host version is incompatible with this Studio build.'}});return;}
    const refresh = req.url === '/api/project/refresh';
    const buildRequest=req.url==='/api/build';
    const write=req.url==='/api/config/save'||req.url==='/api/config/save-as';
    if (req.method !== (refresh||write||buildRequest ? 'POST' : 'GET')) {send(405,{error:'Method not allowed'});return;}
    // All endpoints are argument-free. Reject command/path fields rather than ignoring them.
    if (!write && !buildRequest && (req.headers['transfer-encoding'] || (req.headers['content-length'] && req.headers['content-length'] !== '0'))) {req.resume();send(400,{error:'Request bodies are forbidden'});return;}
    try {
      if(req.url==='/api/source'){if(!reader.readSource)throw new BuildError('Selected source unavailable',404);send(200,await reader.readSource());return;}
      if(eventMatch){if(!reader.build)throw new BuildError('Build unavailable',404);send(200,reader.build.events(eventMatch[1]));return;}
      if(req.url==='/api/build/profile'||req.url==='/api/build/status'){send(200,reader.build?.snapshot()??{protocolVersion:PROTOCOL_VERSION,projectId:reader.snapshot().session.projectId,configured:false,state:'not-configured',reason:'No Host-owned Build Profile selected.',mappingState:'unknown',binaryState:'freshness-unknown',freshnessReason:'No build provenance.',changedInputs:[]});return;}
      if(write||buildRequest){
       if(!buildRequest&&(!reader.snapshot().host.capabilities.writeConfig||!reader.saveConfig||!reader.saveConfigAs)){send(403,{error:{code:'write-failed',message:'This host does not support configuration writes.'}});return;}
       if(req.headers['content-type']!=='application/json'){send(400,{error:{code:'protocol-error',message:'Expected application/json.'}});return;}
       const body=await new Promise<string>((resolve,reject)=>{let size=0;const chunks:Buffer[]=[];let failed=false;const timer=setTimeout(()=>{failed=true;reject(new ConfigError('protocol-error','Request body timed out.'));req.resume();},10000);req.on('data',(b:Buffer)=>{if(failed)return;size+=b.length;if(size>1024*1024){failed=true;clearTimeout(timer);reject(new ConfigError('payload-too-large','Request exceeds 1 MiB.'));return;}chunks.push(b);});req.on('end',()=>{clearTimeout(timer);if(!failed){try{resolve(new TextDecoder('utf-8',{fatal:true}).decode(Buffer.concat(chunks)));}catch{reject(new ConfigError('protocol-error','Request must be valid UTF-8.'));}}});req.on('error',()=>{clearTimeout(timer);reject(new ConfigError('protocol-error','Request body could not be read.'));});});
       let data:unknown;try{data=JSON.parse(body);}catch{throw new ConfigError('protocol-error','Invalid JSON body.');}
       if(!data||typeof data!=='object'||Array.isArray(data))throw new ConfigError('protocol-error','Expected a configuration request.');
       const r=data as Record<string,unknown>;
       if(buildRequest){if(Object.keys(r).length!==2||typeof r.projectId!=='string'||typeof r.profileId!=='string')throw new BuildError('Build accepts only projectId and profileId.');if(!reader.build)throw new BuildError('Build not configured.');send(202,await reader.build.start(r.projectId,r.profileId));return;}
       const save=req.url==='/api/config/save';const keys=save?['projectId','relativePath','expectedFingerprint','text']:['projectId','destinationRelativePath','text'];
       if(Object.keys(r).length!==keys.length||Object.keys(r).some(k=>!keys.includes(k))||typeof r.projectId!=='string'||typeof r.text!=='string'||typeof r[save?'relativePath':'destinationRelativePath']!=='string')throw new ConfigError('protocol-error','Unexpected or missing configuration fields.');
       send(200,save?await reader.saveConfig!(data as SaveConfigRequest):await reader.saveConfigAs!(data as SaveConfigAsRequest));return;
      }
      if(req.url==='/api/config'){if(!reader.readConfig){send(404,{error:'Config unavailable'});return;}send(200,await reader.readConfig());return;}
      const result = refresh ? await reader.refresh() : reader.snapshot();
      if (req.url === '/api/health') send(200,{protocolVersion:result.host.protocolVersion,status:'ready'});
      else if (req.url === '/api/host') send(200,result.host);
      else if (req.url === '/api/project/files') send(200,[result.session.caseSource,result.session.parameterFile,result.session.executable].filter(Boolean));
      else send(200,result);
    } catch(error) {if(error instanceof BuildError)send(error.status,{error:{code:'build-error',message:error.message}});else if(error instanceof ConfigError)send(error.info.code==='not-found'?404:error.info.code==='payload-too-large'?413:['changed-externally','destination-exists'].includes(error.info.code)?409:error.info.code==='permission-denied'?403:400,{error:error.info});else send(503,{error:'Project refresh failed; previous session retained'});}
  });
}
export async function listenLocal(server: Server, port: number): Promise<void> {
  await new Promise<void>((resolve,reject)=>{server.once('error',reject);server.listen(port,'127.0.0.1',()=>{server.off('error',reject);resolve();});});
}
