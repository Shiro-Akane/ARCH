/** Fixed Host-owned worker. No scientific Core or field payload is loaded. */
import {inspectPlotfileMetadata} from './plotfileMetadata.ts';

try {
 if(process.argv.length!==3)throw Error('One local plotfile path is required.');
 const result=await inspectPlotfileMetadata(process.argv[2]);
 process.stdout.write(JSON.stringify({ok:true,result}));
} catch(error) {
 const message=error instanceof Error?error.message:'Metadata inspection failed.';
 process.stdout.write(JSON.stringify({ok:false,message:message.slice(0,1024)}));
 process.exitCode=1;
}
