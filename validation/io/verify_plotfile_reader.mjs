/**
 * Read-only Sod/CellularDet t=0 writer -> Host -> client contract evidence.
 * Usage: node validation/io/verify_plotfile_reader.mjs local-evidence-directories.json [--all-fields]
 * Requires the Studio Node runtime/dependencies; outputs processed JSON only.
 * No UI automation or general scientific certification is performed.
 */
import fs from 'node:fs/promises';
import {inspectPlotfileMetadata,readPlotfileFieldSlice,readPlotfileOverview,readPlotfilePoint} from "../../studio/host/plotfileMetadata.ts";
import {validatePlotfileAudit,validatePlotfileOverview,validatePlotfilePoint} from "../../studio/src/host/plotfileAudit.ts";
import {PROTOCOL_VERSION} from "../../studio/src/host/contracts.ts";
import assert from 'node:assert/strict';
const rows=[];
assert.ok(process.argv.length===3||process.argv.length===4&&process.argv[3]==='--all-fields','Use evidence JSON and optional --all-fields');
const allFields=process.argv[3]==='--all-fields';
for(const directory of JSON.parse(await fs.readFile(process.argv[2],'utf8'))) {
 const output=directory+'/output',files=(await fs.readdir(output)).filter(n=>n.includes('_plt_')&&n.endsWith('.h5'));
 assert.equal(files.length,1,'Exactly one t=0 plotfile is required per evidence directory');
 const path=output+'/'+files[0];
 const metadata=await inspectPlotfileMetadata(path),identity={protocolVersion:PROTOCOL_VERSION,projectId:'field-readback',relativePath:path};
 const m=validatePlotfileAudit({...identity,metadata},identity.projectId,path).audit;
 assert.ok(['Sod','CellularDet'].includes(m.candidateSourceIdentity?.caseId));assert.equal(m.time,0);
 assert.equal(m.fields.find(f=>f.name==='DENS').unit,'g/cm^3');
 assert.equal(m.coordinates.units,'cm');assert.equal(m.timeUnit,'s');assert.equal(m.candidateSourceIdentity.eosUnitSystem,'cgs');
 const properties=m.candidateSourceIdentity.speciesProperties;
 if(properties?.state==='recorded'){
  assert.equal(properties.source,'resolved-runtime-checkpoint-provenance');
  for(const key of ['A','Z','gamma','Cv'])assert.equal(properties.values[key].length,m.candidateSourceIdentity.speciesNames.length);
 }

 assert.equal(m.candidateNativeGrid.measureUnit,m.dimension===1?'cm':'cm^2');
 const selection={field:'DENS',block:0,start:m.cellShape.map(()=>0),count:m.cellShape.map(()=>1)};
 const result=await readPlotfileFieldSlice(path,selection);
 const slice=validatePlotfileAudit({...identity,result},identity.projectId,path,selection,m.file.sha256).audit;
 assert.equal(slice.payload.unit,'g/cm^3');
 const request={field:'DENS',width:32,height:m.dimension===1?1:24},overview=await readPlotfileOverview(path,request);
 validatePlotfileOverview({...identity,result:overview},identity.projectId,path,request,m.file.sha256);
 const query={field:'DENS',point:m.dimension===1?[.49]:[.5,6.5]},point=await readPlotfilePoint(path,query);
 validatePlotfilePoint({...identity,result:point},identity.projectId,path,query,m.file.sha256);
 const exportedFields=[];
 if(allFields)for(const field of m.fields){
  const selected={...selection,field:field.name};
  const raw=await readPlotfileFieldSlice(path,selected);
  validatePlotfileAudit({...identity,result:raw},identity.projectId,path,selected,m.file.sha256);
  assert.equal(raw.payload.unit,field.unit);
  const displayRequest={...request,field:field.name},display=await readPlotfileOverview(path,displayRequest);
  validatePlotfileOverview({...identity,result:display},identity.projectId,path,displayRequest,m.file.sha256);
  const pointRequest={...query,field:field.name},native=await readPlotfilePoint(path,pointRequest);
  validatePlotfilePoint({...identity,result:native},identity.projectId,path,pointRequest,m.file.sha256);
  assert.equal(native.payload.unit,field.unit);
  exportedFields.push({field:field.name,declaration:field.declaration,rawPointValue:native.payload.values[0],
   pointIndex:native.payload.linearIndices[0],overviewScannedCells:display.overview.scannedCells,
   sliceOverviewPointClientValidation:'PASS'});
 }
 rows.push({case:m.candidateSourceIdentity.caseId,fileSha256:m.file.sha256,binarySha256:m.candidateSourceIdentity.binarySha256,
  speciesProperties:m.candidateSourceIdentity.speciesProperties??null,
  dimension:m.dimension,fieldDeclaration:m.fields.find(f=>f.name==='DENS').declaration,
  coordinateUnit:m.coordinates.units,timeUnit:m.timeUnit,measureUnit:m.candidateNativeGrid.measureUnit,
  measureNormalization:m.candidateNativeGrid.measureNormalization,rawPointValue:point.payload.values[0],
  pointIndex:point.payload.linearIndices[0],overviewScannedCells:overview.overview.scannedCells,
  readerClientValidation:'PASS',completion:m.completion.state,renderEligible:m.renderEligible,
  ...(allFields?{exportedFields}:{}),
  scope:allFields?'real t=0 exported field metadata/slice/overview/point; not native UI or science acceptance':'real t=0 DENS metadata/slice/overview/point; not native UI or science acceptance'});
}
console.log(JSON.stringify(rows,null,2));
