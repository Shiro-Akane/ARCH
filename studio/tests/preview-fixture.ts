import {fixture} from './build-fixture.ts';
import {mkdir,writeFile,rm} from 'node:fs/promises';
import {makeManifest,inputs,saveManifest} from '../host/buildManifest.ts';
import {BuildRunner} from '../host/buildRunner.ts';
import {PreviewRunner} from '../host/previewRunner.ts';
import {SOD_PREVIEW_PROFILE,CELLULAR_PREVIEW_PROFILE} from '../host/previewProfile.ts';
import {readFileSync} from 'node:fs';
import {createHash} from 'node:crypto';
export function corePayload(id={requestId:'r',caseId:'Sod',configRevision:'c'}){return {schemaVersion:'1.0',kind:'initial-state-preview',status:'ok',stage:'complete',identity:id,execution:{previewBackend:'cpu',simulationReadiness:'not_checked',timeStepping:'not_executed',scientificOutput:'not_created'},data:{dimension:1,kind:'line',sampling:{kind:'uniform',valueLocation:'init-sample',position:'bin-center',count:2,shape:[2],order:'x1-fastest'},axes:[{name:'x1',unit:null,values:[.25,.75]}],fields:[{key:'DENS',displayName:'Density',unit:null,values:[1,.125],min:.125,max:1}]},diagnostics:[]};}
export async function previewFixture(mode='ok',timeoutMs=1000,cellular=false){
 const {root,p}=await fixture();await mkdir(root+'/studio');
 const payload=cellular?JSON.parse(readFileSync(new URL('../../src/api/examples/core-b/cellular-x1.json',import.meta.url),'utf8')):corePayload();
 const caps=cellular?JSON.parse(readFileSync(new URL('../../src/api/examples/core-b/capabilities.json',import.meta.url),'utf8')):{schemaVersion:'1.0',kind:'preview-capabilities',cases:['Sod'],dimensions:[1]};
 const script=`#!${process.execPath}
if(process.argv.includes('--preview-capabilities')){console.log(JSON.stringify(${JSON.stringify(caps)}));process.exit(0);}
const crypto=require('node:crypto');let input='';process.stdin.setEncoding('utf8');process.stdin.on('data',b=>input+=b);process.stdin.on('end',()=>{const mode=${JSON.stringify(mode)};const result=${JSON.stringify(payload)};result.identity={requestId:process.argv[process.argv.indexOf('--request-id')+1],caseId:${JSON.stringify(cellular?'CellularDet':'Sod')},configRevision:crypto.createHash('sha256').update(input).digest('hex')};if(mode==='hang'){process.on('SIGTERM',()=>{});setInterval(()=>{},100);return;}if(mode==='bad'){console.log('invalid');return;}if(mode==='huge'){process.stdout.write('x'.repeat(9*1024*1024));return;}if(mode==='exit'){process.exit(6);}if(input.includes('force_failure')){result.status='error';result.stage='eos';result.data=null;result.diagnostics=[{severity:'error',code:'EOS_FAILED',message:'fixture failure'}];console.log(JSON.stringify(result));process.exitCode=5;return;}if(mode==='wrong')result.identity.configRevision='wrong';setTimeout(()=>console.log(JSON.stringify(result)),mode==='slow'?200:0);});
`;
 await writeFile(root+'/build/bin/ARCH',script,{mode:0o755});
 if(cellular)p.caseId='CellularDet';
 const manifest=await makeManifest(p,'p','build-id',new Date().toISOString(),await inputs(p),undefined,{});await saveManifest(p,manifest);
 const build=new BuildRunner(root,'p',p);await build.initialize();const preview=new PreviewRunner(build,{...(cellular?CELLULAR_PREVIEW_PROFILE:SOD_PREVIEW_PROFILE),buildProfileId:p.id},{timeoutMs,graceMs:30});build.executionBlocked=()=>preview.isActive();
 const configText='x_pos=.3\n';const request={projectId:'p',profileId:cellular?CELLULAR_PREVIEW_PROFILE.id:SOD_PREVIEW_PROFILE.id,configText,configRevision:createHash('sha256').update(configText).digest('hex'),...(cellular?{requestedShape:payload.data.sampling.shape}:{requestedSampleCount:2})};
 return {root,p,build,preview,request,cleanup:()=>rm(root,{recursive:true,force:true})};
}
export async function previewFinished(r:PreviewRunner){const until=Date.now()+4000;while(r.isActive()&&Date.now()<until)await new Promise(resolve=>setTimeout(resolve,10));if(r.isActive())throw new Error('Preview did not terminate');return r.snapshot();}
