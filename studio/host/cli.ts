import {parseArgs} from 'node:util';
import {openProject} from './project.ts';import {createHostServer,listenLocal} from './server.ts';
try {
 const {values}=parseArgs({options:{project:{type:'string'},case:{type:'string'},config:{type:'string'},binary:{type:'string'},port:{type:'string',default:'4180'},origin:{type:'string',default:'http://127.0.0.1:5173'}},strict:true,allowPositionals:false});
 if(!values.project)throw new Error('--project is required');const port=Number(values.port);if(!Number.isInteger(port)||port<1024||port>65535)throw new Error('Invalid port');
 const reader=await openProject({project:values.project,case:values.case,config:values.config,binary:values.binary});
 const server=createHostServer(reader,values.origin!);await listenLocal(server,port);
 console.log(`ARCH Local Host protocol 1.1 at http://127.0.0.1:${port}; authorized UI ${values.origin}; authorized project ${reader.snapshot().host.projectRoot}`);
 for(const signal of ['SIGINT','SIGTERM'] as const)process.on(signal,()=>server.close(()=>process.exit(0)));
} catch(error){console.error(`Local Host startup failed: ${error instanceof Error?error.message:'unknown error'}`);process.exitCode=1;}
