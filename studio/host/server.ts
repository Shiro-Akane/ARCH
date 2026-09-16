import { createServer } from 'node:http';
import type { Server } from 'node:http';
import type { ProjectSnapshot } from '../src/host/contracts.ts';
export interface ProjectReader { snapshot(): ProjectSnapshot; refresh(): Promise<ProjectSnapshot> }
export function createHostServer(reader: ProjectReader, origin: string): Server {
  const allowed = new URL(origin);
  if (allowed.protocol !== 'http:' || allowed.hostname !== '127.0.0.1' || allowed.origin !== origin) throw new Error('UI origin must be an exact http://127.0.0.1:PORT origin');
  return createServer(async (req, res) => {
    const send = (status: number, value: unknown) => {res.writeHead(status, {'Content-Type':'application/json', 'Cache-Control':'no-store', 'X-Content-Type-Options':'nosniff'});res.end(JSON.stringify(value));};
    const address = req.socket.localPort;
    if (req.headers.host !== `127.0.0.1:${address}` || req.headers.origin !== origin) {send(403,{error:'Host or Origin rejected'});return;}
    res.setHeader('Access-Control-Allow-Origin',origin);
    res.setHeader('Vary','Origin');
    const routes = ['/api/health','/api/host','/api/project','/api/project/files','/api/project/refresh'];
    if (!routes.includes(req.url ?? '')) {send(404,{error:'Unknown endpoint'});return;}
    if (req.method === 'OPTIONS') {res.setHeader('Access-Control-Allow-Methods','GET, POST');res.setHeader('Access-Control-Allow-Headers','X-ARCH-Studio');send(200,{});return;}
    if (req.headers['x-arch-studio'] !== '1') {send(403,{error:'Studio request header required'});return;}
    const refresh = req.url === '/api/project/refresh';
    if (req.method !== (refresh ? 'POST' : 'GET')) {send(405,{error:'Method not allowed'});return;}
    // All endpoints are argument-free. Reject command/path fields rather than ignoring them.
    if (req.headers['transfer-encoding'] || (req.headers['content-length'] && req.headers['content-length'] !== '0')) {req.resume();send(400,{error:'Request bodies are forbidden'});return;}
    try {
      const result = refresh ? await reader.refresh() : reader.snapshot();
      if (req.url === '/api/health') send(200,{protocolVersion:result.host.protocolVersion,status:'ready'});
      else if (req.url === '/api/host') send(200,result.host);
      else if (req.url === '/api/project/files') send(200,[result.session.caseSource,result.session.parameterFile,result.session.executable].filter(Boolean));
      else send(200,result);
    } catch {send(503,{error:'Project refresh failed; previous session retained'});}
  });
}
export async function listenLocal(server: Server, port: number): Promise<void> {
  await new Promise<void>((resolve,reject)=>{server.once('error',reject);server.listen(port,'127.0.0.1',()=>{server.off('error',reject);resolve();});});
}
