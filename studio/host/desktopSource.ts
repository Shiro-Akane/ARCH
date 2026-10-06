import path from 'node:path';
import {fingerprint} from './files.ts';
import type {RegisteredCase} from '../src/host/workflowContracts.ts';
/** Match authoritative registry paths; never infer a case from a basename. */
export function registeredSourceCase(cases:readonly {caseId:string;inspection:{sourceFile:string|null}}[],root:string,source:string,requested?:string){
 const absolute=path.resolve(root,source);
 const rel=path.relative(root,absolute);
 if(rel.startsWith('../')||path.isAbsolute(rel))throw new Error('Source is outside the managed project.');
 const matches=cases.filter(c=>c.inspection.sourceFile!==null&&path.resolve(root,c.inspection.sourceFile)===absolute);
 if(matches.length!==1)throw new Error('Source has no unique authoritative compiled case association. Choose a registered source.');
 if(requested&&requested!==matches[0].caseId)throw new Error('Case and source association disagree.');
 return matches[0].caseId;
}

/** Opening a desktop project is not permission to execute Init/AMR.
 * Static registration uses the selected binary; execution retains its Build gate. */
export async function desktopRegistry(reader:{
 configuration?:{discovery:()=>Promise<import('../src/host/workflowContracts.ts').DiscoveryResponse>};
 workflow?:{discovery:()=>Promise<import('../src/host/workflowContracts.ts').DiscoveryResponse>};
}){
 if(reader.configuration)return reader.configuration.discovery();
 if(reader.workflow)return reader.workflow.discovery();
 throw new Error('Selected binary registration unavailable.');
}

/** Bind only the exact selected source and compiled digest; edited C++ stays pending.
 * This guard runs before configuration/initialization/run requests, and again
 * before terminal handoff. No macro, filename or current Git HEAD implies a case.
 */
export async function requireCompiledSourceCase(root:string,source:string,cases:readonly RegisteredCase[],requested?:string){
 const file=await fingerprint(root,source,'case-source');
 if(!file.exists||file.error||!file.sha256)throw new Error(file.error??'Selected source is unavailable.');
 const caseId=registeredSourceCase(cases,root,source,requested);
 const match=cases.find(c=>c.caseId===caseId)!;
 if(match.inspection.compiledSourceSha256!==file.sha256)throw new Error('Selected source differs from its compiled registration. Configure/Build before using this case.');
 return caseId;
}
