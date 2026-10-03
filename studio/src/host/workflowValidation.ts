import {validateCoordinates} from './configurationValidation.ts';
import {record} from './previewValidation.ts';
import type {RegisteredCase,ResourceEstimate,AmrMesh,WorkflowCore,WorkflowOperation,CaseProbe} from './workflowContracts.ts';
function invalid(message:string):never{throw new Error(message);}
const finite=(v:unknown):v is number=>typeof v==='number'&&Number.isFinite(v);
const integer=(v:unknown,min=0,max=Number.MAX_SAFE_INTEGER):v is number=>finite(v)&&Number.isInteger(v)&&v>=min&&v<=max;
const texts=(v:unknown):v is string[]=>Array.isArray(v)&&v.every(x=>typeof x==='string');
const numbers=(v:unknown,n:number):v is number[]=>Array.isArray(v)&&v.length===n&&v.every(finite);
export function validateRegistry(value:unknown):RegisteredCase[]{
 if(!record(value)||value.schemaVersion!=='1.0'||value.version!=='1'||value.kind!=='registered-cases'||value.status!=='ok'||value.setup!=='not_executed'||value.cuda!=='not_initialized'||!Array.isArray(value.cases)||value.cases.length>1024)invalid('Invalid binary case registry.');
 const ids=new Set<string>();
 for(const c of value.cases){
  if(!record(c)||typeof c.caseId!=='string'||!c.caseId||c.caseId.length>128||ids.has(c.caseId)||typeof c.initialFieldPreview!=='boolean'||typeof c.initialAmrPreview!=='boolean'||!Array.isArray(c.previewDimensions)||c.previewDimensions.some(d=>![1,2,3].includes(Number(d))))invalid('Invalid registered case.');
  const i=c.inspection;
  if(!record(i)||i.registered!==true||i.command!=='--inspect-case'||i.workerPlatform!=='linux'||typeof i.setupReads!=='boolean'||typeof i.primitiveSinkProbe!=='boolean'||i.automaticExpressionInference!==false||!integer(i.hostWallTimeoutSeconds,1,360)||typeof i.coverage!=='string'||typeof i.reviewedUnitEvidence!=='string'||(i.sourceFile!==null&&typeof i.sourceFile!=='string')||(i.compiledSourceSha256!==null&&(typeof i.compiledSourceSha256!=='string'||!/^[a-f0-9]{64}$/.test(i.compiledSourceSha256))))invalid('Invalid case inspection capability.');
  ids.add(c.caseId);
 }
 return value.cases as unknown as RegisteredCase[];
}
export function validateResources(value:unknown):ResourceEstimate{
 if(!record(value)||value.version!=='1'||value.advisoryOnly!==true||value.oomPrediction!=='not-provided'||!integer(value.dimension,1,3)||!Array.isArray(value.levels)||value.levels.length>128||typeof value.assumption!=='string'||typeof value.scope!=='string'||!texts(value.excludes))invalid('Invalid AMR resource estimate.');
 // Core int64 counts can exceed JavaScript exact integer precision; UI marks these approximate.
 const count=(x:unknown)=>x===null||integer(x,0,Number.MAX_VALUE);
 if(!count(value.poolPreallocatedBaseBytes)||!integer(value.configuredPoolCapacity,0,Number.MAX_VALUE)||!count(value.speciesCount))invalid('Invalid resource count.');
 let previous=-1;
 for(const l of value.levels){
  if(!record(l)||!integer(l.level,0,127)||l.level<=previous||typeof l.overflow!=='boolean'||!['fullDomainLeafBlocks','activeCells','baseStateBytes','stateBytesIncludingSpecies'].every(k=>count(l[k])))invalid('Invalid resource level.');
  previous=l.level;
  if(!l.overflow&&['fullDomainLeafBlocks','activeCells','baseStateBytes'].some(k=>l[k]===null))invalid('Missing finite resource estimate.');
 }
 return value as unknown as ResourceEstimate;
}
export function validateMesh(value:unknown,status:string):AmrMesh{
 if(!record(value)||value.version!=='1'||value.kind!=='amr-leaf-mesh'||![1,2,3].includes(Number(value.dimension))||!['cartesian','spherical','cylindrical'].includes(String(value.geometry))||(value.unit!==null&&typeof value.unit!=='string')||typeof value.complete!=='boolean'||!integer(value.completedPasses)||!integer(value.leafCount,0,1024)||!Array.isArray(value.leaves)||value.leaves.length!==value.leafCount||!Array.isArray(value.levelCounts)||!integer(value.configuredMaxBlocks,1)||!integer(value.workingCapacity,0,1024)||!['none','last-completed-balanced-hierarchy'].includes(String(value.snapshot))||(value.limitedReason!==null&&typeof value.limitedReason!=='string'))invalid('Invalid initial AMR mesh.');
 if((status==='ok')!==value.complete||(value.complete&&(value.snapshot==='none'||value.limitedReason!==null))||(!value.complete&&typeof value.limitedReason!=='string')||(value.snapshot==='none'&&value.leafCount!==0))invalid('Contradictory AMR completion state.');
 if(value.coordinates!==undefined){
  const c=value.coordinates;
  if(!record(c)||c.version!=='1'||c.basis!=='native-grid')invalid('Invalid AMR coordinate contract.');
  validateCoordinates(c.metadata);
  if(!record(c.metadata)||c.metadata.dimension!==value.dimension||c.metadata.geometry!==value.geometry
   ||!Array.isArray(c.metadata.axes)||c.metadata.axes.some((a,i)=>!record(a)||a.active!==(i<Number(value.dimension))))invalid('AMR native coordinates disagree with mesh.');
 }else if(value.geometry!=='cartesian'||value.dimension===3)invalid('AMR native coordinate metadata required.');
 if(value.geometry!=='cartesian'&&value.unit!==null)invalid('Mixed AMR axis units cannot use one length unit.');
 const ids=new Set<string>(),counts=new Map<number,number>(),dimension=Number(value.dimension);
 for(const l of value.leaves){
  if(!record(l)||!integer(l.level,0,127)||!numbers(l.logicalIndex,3)||!l.logicalIndex.every(n=>integer(n))||l.logicalKey!==[l.level,...l.logicalIndex].join(':')||ids.has(String(l.logicalKey))||!numbers(l.lower,dimension)||!numbers(l.upper,dimension)||!numbers(l.cellShape,dimension)||!l.cellShape.every(n=>integer(n,1,65536))||!numbers(l.cellSpacing,dimension))invalid('Invalid AMR leaf identity or geometry.');
  for(let i=0;i<dimension;i++)if(l.upper[i]<=l.lower[i]||l.cellSpacing[i]<=0||Math.abs((l.upper[i]-l.lower[i])/l.cellShape[i]-l.cellSpacing[i])>1e-9*Math.max(l.cellSpacing[i],Number.MIN_VALUE))invalid('Inconsistent AMR cell spacing.');
  ids.add(String(l.logicalKey));counts.set(l.level,(counts.get(l.level)??0)+1);
 }
 const levels=new Set<number>();
 for(const l of value.levelCounts){if(!record(l)||!integer(l.level)||levels.has(l.level)||!integer(l.leafBlocks,0,1024)||(counts.get(l.level)??0)!==l.leafBlocks)invalid('AMR level counts disagree with leaves.');levels.add(l.level);}
 if([...counts.keys()].some(level=>!levels.has(level)))invalid('Incomplete AMR level counts.');
 const resources=validateResources(value.resources);
 if(resources.dimension!==dimension)invalid('AMR resource dimension mismatch.');
 return value as unknown as AmrMesh;
}
function validateProbe(value:unknown):CaseProbe{
 if(!record(value)||value.kind!=='initial-primitive-probe'||value.completeFieldCoverage!==false||!integer(value.sampleCount,1,27)||!Array.isArray(value.samples)||value.samples.length!==value.sampleCount||typeof value.valueLocation!=='string'||typeof value.sampling!=='string'||typeof value.velocityBasis!=='string')invalid('Invalid Init probe.');
 for(const s of value.samples){
  if(!record(s)||!numbers(s.cartesianPosition,3)||typeof s.positionUnit!=='string'||typeof s.thermodynamicInput!=='string'||!Array.isArray(s.fields)||s.fields.length>32||!Array.isArray(s.massFractions)||!s.massFractions.every(finite)||typeof s.massFractionUnit!=='string')invalid('Invalid Init sample.');
  for(const f of s.fields)if(!record(f)||typeof f.key!=='string'||(f.unit!==null&&typeof f.unit!=='string')||!finite(f.value)||typeof f.consumedByConversion!=='boolean')invalid('Invalid Init sample field.');
 }
 return value as unknown as CaseProbe;
}
export function validateWorkflowCore(value:unknown,operation:WorkflowOperation,expected:{requestId:string;caseId:string;configRevision:string}):WorkflowCore{
 const kind={'inspect-case':'case-inspection','amr-resources':'amr-resource-estimate','preview-amr':'initial-amr-preview'}[operation];
 if(!record(value)||value.schemaVersion!=='1.0'||value.kind!==kind||!['ok','limited','error'].includes(String(value.status))||!record(value.identity)||!Object.entries(expected).every(([k,v])=>record(value.identity)&&value.identity[k]===v)||!record(value.execution)||!Array.isArray(value.diagnostics))invalid('Workflow response or identity mismatch.');
 for(const d of value.diagnostics)if(!record(d)||!['info','warning','error'].includes(String(d.severity))||typeof d.code!=='string'||typeof d.message!=='string')invalid('Invalid workflow diagnostic.');
 const ex=value.execution;
 if(operation==='amr-resources'){
  if(value.version!=='1'||ex.setup!=='not_executed'||ex.eos!=='not_loaded'||ex.cuda!=='not_initialized'||value.status==='limited')invalid('Invalid resource execution contract.');
  if(value.status==='ok')validateResources(value.data);
 }else{
  if(ex.previewBackend!=='cpu'||ex.timeStepping!=='not_executed'||ex.simulationReadiness!=='not_checked')invalid('Unsafe workflow execution contract.');
  if(operation==='preview-amr'){
   if(ex.scientificOutput!=='not_created')invalid('AMR scientific output contract mismatch.');
   if(value.status!=='error'){
    const mesh=validateMesh(value.data,String(value.status)),grid=record(value.state)?value.state.grid:undefined;
    if(record(grid)&&(grid.dimension!==mesh.dimension||grid.geometry!==mesh.geometry))invalid('AMR state grid disagrees with mesh.');
   }
  }else{
   if(value.version!=='1'||ex.driverScientificOutput!=='not_created'||ex.eosConversion!=='not_executed'||value.status==='limited')invalid('Invalid inspection execution contract.');
   if(value.data!==null)validateProbe(value.data);
   const m=value.parameterMetadata;
   if(m!==undefined){
    if(!record(m)||m.version!=='1'||m.complete!==false||m.automaticExpressionInference!==false||typeof m.coverage!=='string'||!Array.isArray(m.parameters)||m.parameters.length>4096||!texts(m.unobservedInputKeys)||typeof m.unobservedMeaning!=='string')invalid('Invalid observed metadata.');
    for(const p of m.parameters){
     if(!record(p)||typeof p.key!=='string'||(p.type!==null&&typeof p.type!=='string')||!['explicit','default','unknown'].includes(String(p.valueSource))||!integer(p.readCount,1)||(p.unit!==null&&typeof p.unit!=='string')||!record(p.unitEvidence)||p.unitEvidence.automaticInference!==false||!Array.isArray(p.diagnostics))invalid('Invalid observed parameter.');
    }
   }
  }
 }
 if(value.status==='error'&&value.data!==null)invalid('Failed workflow cannot publish data.');
 return value as unknown as WorkflowCore;
}
