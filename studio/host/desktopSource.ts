import path from 'node:path';
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
