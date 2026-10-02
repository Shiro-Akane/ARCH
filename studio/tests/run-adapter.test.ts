import test from 'node:test';
import assert from 'node:assert/strict';
import {readFile} from 'node:fs/promises';
import {validateRunPreparation,validateRunAcceptance,validateRunStatus} from '../src/host/RunAdapter.ts';
import {PROTOCOL_VERSION} from '../src/host/contracts.ts';
const id='11111111-1111-4111-8111-111111111111';
const expected={projectId:'p',caseId:'Sod',mode:'run' as const,configRevision:'a'.repeat(64),configPath:'case.par',binaryPath:'bin/ARCH'};
async function fixture(){
 const inspection=JSON.parse(await readFile(new URL('../../src/api/examples/configuration-v3/sod-valid.json',import.meta.url),'utf8'));
 inspection.identity={caseId:'Sod',configRevision:expected.configRevision,requestId:id};
 const fingerprint={sha256:expected.configRevision,size:4,modifiedTime:'2026-10-02T00:00:00Z'};
 return {protocolVersion:PROTOCOL_VERSION,projectId:'p',planId:id,caseId:'Sod',mode:'run',createdAt:'2026-10-02T00:00:00Z',
  binary:{relativePath:'bin/ARCH',fingerprint,sourceClaim:'compiled-version-only'},config:{relativePath:'case.par',fingerprint},
  inspection,pathChecks:[],issues:[],canConfirm:true,simulationReadiness:'core-startup-pending',checkpointPath:null,
  pendingChecks:['Core startup']};
}
test('client Run plan rejects stale input/project/binary and false readiness claims',async()=>{
 const good=await fixture();assert.equal(validateRunPreparation(good,expected).planId,id);
 for(const bad of [
  {...good,projectId:'other'},
  {...good,caseId:'Other'},
  {...good,binary:{...good.binary,relativePath:'other/ARCH'}},
  {...good,binary:{...good.binary,sourceClaim:'current'}},
  {...good,config:{...good.config,fingerprint:{...good.config.fingerprint,sha256:'b'.repeat(64)}}},
  {...good,issues:['error']},
  {...good,simulationReadiness:'ready'},
  {...good,inspection:{...good.inspection,status:'error',completeness:{...good.inspection.completeness,state:'invalid'}}},
 ])assert.throws(()=>validateRunPreparation(bad,expected));
});
test('client handoff/status keeps exact run ownership and verified completion semantics',()=>{
 const state={runId:id,state:'succeeded',workerPid:10,startedAt:'2026-10-02T00:00:00Z',finishedAt:'2026-10-02T00:00:01Z',exitCode:0};
 const accepted={protocolVersion:PROTOCOL_VERSION,projectId:'p',runId:id,terminalPid:9,state};
 assert.equal(validateRunAcceptance(accepted,'p').runId,id);
 assert.equal(validateRunStatus({...state,protocolVersion:PROTOCOL_VERSION,projectId:'p'},'p',id).exitCode,0);
 for(const bad of [{...accepted,projectId:'other'},{...accepted,state:{...state,runId:'other'}},
  {...accepted,state:{...state,exitCode:1}},{...accepted,state:{...state,finishedAt:undefined}}])
  assert.throws(()=>validateRunAcceptance(bad,'p'));
});

import {validateRunHistory} from '../src/host/RunAdapter.ts';
test('history preserves unknown status and rejects stale projects or invented successful records',()=>{
 const item={runId:'aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa',state:null,diagnostic:'Supervisor unavailable'};
 const response={protocolVersion:PROTOCOL_VERSION,projectId:'p',records:[item]};
 assert.equal(validateRunHistory(response,'p')[0].state,null);
 assert.throws(()=>validateRunHistory(response,'old'));
 assert.throws(()=>validateRunHistory({...response,records:[{...item,diagnostic:null}]},'p'));
 assert.throws(()=>validateRunHistory({...response,records:[item,item]},'p'));
});
