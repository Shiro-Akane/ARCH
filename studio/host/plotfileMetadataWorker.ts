/** Fixed Host-owned reader worker. No scientific Core execution. */
import {inspectPlotfileMetadata,readPlotfileFieldSlice} from './plotfileMetadata.ts';
import {copyPlotfileSliceRequest} from './plotfileSliceRequest.ts';

try {
 if(![3,4].includes(process.argv.length))throw Error('One local plotfile path and optional bounded slice are required.');
 const result=process.argv.length===3
  ?await inspectPlotfileMetadata(process.argv[2])
  :await readPlotfileFieldSlice(process.argv[2],copyPlotfileSliceRequest(JSON.parse(process.argv[3])));
 process.stdout.write(JSON.stringify({ok:true,result}));
} catch(error) {
 const message=error instanceof Error?error.message:'Plotfile read failed.';
 process.stdout.write(JSON.stringify({ok:false,message:message.slice(0,1024)}));
 process.exitCode=1;
}
