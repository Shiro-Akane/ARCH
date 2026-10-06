import {stringifyPlotfile} from './plotfileJson.ts';
import {copyPointRequest} from '../src/host/plotfilePoint.ts';
/** Fixed Host-owned reader worker. No scientific Core execution. */
import {inspectPlotfileMetadata,readPlotfileFieldSlice,readPlotfileOverview,readPlotfilePoint} from './plotfileMetadata.ts';
import {copyOverviewRequest} from '../src/host/plotfileOverview.ts';
import {copyPlotfileSliceRequest} from './plotfileSliceRequest.ts';

try {
 if(![3,4].includes(process.argv.length))throw Error('One local plotfile path and optional bounded slice are required.');
 const request=process.argv.length===4?JSON.parse(process.argv[3]):undefined;
 const isPoint=request&&Object.keys(request).join(',')==='pointQuery';
 const isOverview=request&&Object.keys(request).join(',')==='overview';
 const result=request===undefined?await inspectPlotfileMetadata(process.argv[2])
  :isPoint?await readPlotfilePoint(process.argv[2],copyPointRequest(request.pointQuery))
  :isOverview?await readPlotfileOverview(process.argv[2],copyOverviewRequest(request.overview))
  :await readPlotfileFieldSlice(process.argv[2],copyPlotfileSliceRequest(request));
 process.stdout.write(stringifyPlotfile({ok:true,result}));
} catch(error) {
 const message=error instanceof Error?error.message:'Plotfile read failed.';
 process.stdout.write(JSON.stringify({ok:false,message:message.slice(0,1024)}));
 process.exitCode=1;
}
