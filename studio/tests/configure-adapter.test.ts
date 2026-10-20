import test from 'node:test';
import assert from 'node:assert/strict';
import {validateConfigureStatus} from '../src/host/ConfigureAdapter.ts';
import {PROTOCOL_VERSION} from '../src/host/contracts.ts';
test('Configure response requires current project/version and truthful operation identity',()=>{
 const base={protocolVersion:PROTOCOL_VERSION,projectId:'p',active:false,available:false};
 assert.equal(validateConfigureStatus(base,'p').available,false);
 for(const value of [{...base,projectId:'old'},{...base,protocolVersion:'old'},{...base,active:true}]){
  assert.throws(()=>validateConfigureStatus(value,'p'));
 }
 const id='00000000-0000-0000-0000-000000000001';
 const success={protocolVersion:PROTOCOL_VERSION,projectId:'p',active:false,profileId:'cpu',operationId:id,latest:{id,profileId:'cpu',state:'succeeded',exitCode:0}};
 assert.equal(validateConfigureStatus(success,'p').latest?.state,'succeeded');
 assert.throws(()=>validateConfigureStatus({...success,latest:{...success.latest,exitCode:1}},'p'));
 assert.throws(()=>validateConfigureStatus({...success,operationId:'old'},'p'));
});
