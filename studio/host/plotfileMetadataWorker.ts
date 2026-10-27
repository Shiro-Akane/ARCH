import type {PlotfileReadContext} from './plotfileMetadata.ts';
import {windowsEnvironment} from './hostProcess.ts';
/**
 * Execute one fixed read-only Plotfile query in the isolated reader process.
 * The parent owns path scope, cancellation and resource limits. Copy the typed
 * request, invoke the shared reader, serialize one bounded result and exit;
 * no arbitrary module, command or scientific Core request is accepted.
 */
import {stringifyPlotfile} from './plotfileJson.ts';
import {copyPointRequest} from '../src/host/plotfilePoint.ts';
/** Fixed Host-owned reader worker. No scientific Core execution. */
import {inspectPlotfileMetadata,readPlotfileFieldSlice,readPlotfileOverview,readPlotfilePoint} from './plotfileMetadata.ts';
import {copyOverviewRequest} from '../src/host/plotfileOverview.ts';
import {copyPlotfileSliceRequest} from './plotfileSliceRequest.ts';

try {
 const args=process.argv.slice(2),context:PlotfileReadContext={};
 if(process.platform==='win32'){
  if(args.shift()!=='--native-tools')throw Error('Trusted native Plotfile tools required.');
  context.windowsTools=JSON.parse(args.shift()??'null');
  windowsEnvironment(context.windowsTools!);
 }
 if(![1,2].includes(args.length))throw Error('One local plotfile path and optional bounded slice are required.');
 const request=args.length===2?JSON.parse(args[1]):undefined;
 const isPoint=request&&Object.keys(request).join(',')==='pointQuery';
 const isOverview=request&&Object.keys(request).join(',')==='overview';
 const result=request===undefined?await inspectPlotfileMetadata(args[0],context)
  :isPoint?await readPlotfilePoint(args[0],copyPointRequest(request.pointQuery),context)
  :isOverview?await readPlotfileOverview(args[0],copyOverviewRequest(request.overview),context)
  :await readPlotfileFieldSlice(args[0],copyPlotfileSliceRequest(request),context);
 process.stdout.write(stringifyPlotfile({ok:true,result}));
} catch(error) {
 const message=error instanceof Error?error.message:'Plotfile read failed.';
 process.stdout.write(JSON.stringify({ok:false,message:message.slice(0,1024)}));
 process.exitCode=1;
}
