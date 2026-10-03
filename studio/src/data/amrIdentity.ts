import type {WorkflowResult} from '../host/workflowContracts.ts';
import type {RealPreviewResult} from '../host/previewContracts.ts';
import {record} from '../host/previewValidation.ts';
/** Request IDs differ across field/mesh; all scientific input and binary identities must agree. */
export function matchingAmrField(mesh:WorkflowResult|undefined,field:RealPreviewResult|undefined):boolean{
 if(!mesh||!field||mesh.operation!=='preview-amr'||mesh.core.status==='error'||!mesh.core.data)return false;
 const a=mesh.identity,b=field.identity;
 if(!['projectId','caseId','configRevision','buildId','binarySha256'].every(k=>a[k as keyof typeof a]===b[k as keyof typeof b]))return false;
 const data=mesh.core.data;
 if(!('dimension' in data)||data.dimension!==field.core.data?.dimension)return false;
 const fieldData=field.core.data;
 if('geometry' in data){
  const geometry=fieldData?.coordinates?.geometry??'cartesian';
  if(data.geometry!==geometry)return false;
  if('coordinates' in data&&data.coordinates&&typeof data.coordinates==='object'&&'metadata' in data.coordinates){
   const coords=data.coordinates.metadata as {axes?:{key:string;unit:string|null}[]};
   if(!fieldData?.coordinates||coords.axes?.some((a,i)=>a.key!==fieldData.coordinates!.metadata.axes[i]?.key||a.unit!==fieldData.coordinates!.metadata.axes[i]?.unit))return false;
  }else if(data.dimension===3||geometry!=='cartesian')return false;
 }
 const x=mesh.core.state?.eos,y=field.core.state?.eos;
 if(!record(x)||!record(y)||x.status!=='ready'||y.status!=='ready'||x.requested!==y.requested||x.resolved!==y.resolved)return false;
 // null is authoritative only for a successfully initialized ideal EOS, not missing evidence.
 if(x.resolved==='ideal')return x.sourceFingerprint===null&&y.sourceFingerprint===null;
 return typeof x.sourceFingerprint==='string'&&x.sourceFingerprint.length>0&&x.sourceFingerprint===y.sourceFingerprint;
}
