/** Audit historical candidate gates and optional formal RZ read-only capabilities. */
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import crypto from 'node:crypto';
import {inspectPlotfileMetadataIsolated,readPlotfileFieldSliceIsolated,readPlotfilePointIsolated} from '../../studio/host/isolatedPlotfileMetadata.ts';
const formalOnly=process.argv.length===5&&process.argv[2]==='--formal-rz';
assert.ok(formalOnly||process.argv.length===4||(process.argv.length===6&&process.argv[4]==='--formal-rz'),
 'Use candidate fixture root and new summary JSON [--formal-rz actual.h5], or --formal-rz actual.h5 new-summary.json');
import {sourceEvidenceValid} from '../../studio/src/host/plotfileSourceIdentity.ts';
const root=process.argv[2];
const hash=async p=>crypto.createHash('sha256').update(await fs.readFile(p)).digest('hex');
const find=async mode=>root+'/'+mode+'/'+(await fs.readdir(root+'/'+mode)).find(n=>n.includes('_plt_')&&n.endsWith('.h5'));

/** Preserve the original historical candidate acceptance/rejection assertions. */
async function inspectHistoricalCandidates(){
const cart=await find('cartesian'),rz=await find('rz');
const cartBefore=await hash(cart),rzBefore=await hash(rz);
const m=await inspectPlotfileMetadataIsolated(cart);
assert.equal(m.geometry,'cartesian');
assert.equal(m.candidateNativeGrid.version,'candidate-cartesian-1');
let rejected;
try {await inspectPlotfileMetadataIsolated(rz);}
catch(error){rejected=error;}
assert.equal(rejected?.code,'WORKER_FAILED');
assert.match(rejected.message,/Unsupported candidate native geometry/);
assert.equal(await hash(cart),cartBefore);assert.equal(await hash(rz),rzBefore);

return {status:'PASS',cartesianMetadata:'accepted',internalRzMetadata:'explicitly rejected',
 errorCode:rejected.code,diagnostic:rejected.message,fileHashesUnchanged:true,
 scope:'Historical candidate Cartesian acceptance / partial-native RZ rejection only'};
}

/** Actual formal writer evidence is distinct from historical candidate acceptance. */
async function inspectFormalRz(path){
 const before=await fs.stat(path,{bigint:true});
 assert.ok(before.isFile()&&before.size>0n&&before.size<=64n*1024n*1024n);
 const digest=await hash(path),metadata=await inspectPlotfileMetadataIsolated(path);
 assert.equal(metadata.file.sha256,digest);
 assert.equal(metadata.geometry,'cylindrical');assert.equal(metadata.dimension,2);
 const native=metadata.candidateNativeGrid,source=metadata.candidateSourceIdentity;
 assert.equal(native.version,'arch-native-axisymmetric-rz-2');
 assert.equal(native.chart,'axisymmetric-rz');assert.deepEqual(native.axes,['r','z','inactive']);
 assert.deepEqual(native.axisUnits,['cm','cm','inactive']);
 assert.equal(native.measureNormalization,'full_rotation');assert.equal(native.measureUnit,'cm^3');
 assert.equal(native.measureSource,'GridMetrics::CellVolume');
 assert.ok(sourceEvidenceValid(source));assert.equal(source.version,'arch-plot-identity-1');
 assert.equal(source.recordsVerified,true);assert.equal(source.eosUnitSystem,'cgs');
 assert.equal(metadata.completion.state,'complete');
 assert.equal(metadata.nativeCellGeometry.bounds,'recorded');assert.equal(metadata.nativeCellGeometry.volume,'recorded');
 const units={DENS:'g/cm^3',VELX:'cm/s',VELY:'cm/s',VELZ:'cm/s',ENER:'erg/cm^3',
  PRES:'erg/cm^3',TEMP:'K',JENS:'1',GPOT:'cm^2/s^2',GACX:'cm/s^2',GACY:'cm/s^2',GACZ:'cm/s^2'};
 for(const name of ['DENS','VELX','VELY','VELZ','ENER'])assert.ok(metadata.fields.some(f=>f.name===name));
 for(const field of metadata.fields)if(Object.hasOwn(units,field.name))assert.equal(field.unit,units[field.name]);
 const request={field:'DENS',block:0,start:metadata.cellShape.map(()=>0),count:metadata.cellShape.map(()=>1)};
 // Each isolated query invokes the unchanged production client validators.
 const slice=await readPlotfileFieldSliceIsolated(path,request);
 assert.equal(slice.file.sha256,digest);assert.deepEqual(slice.candidateSourceIdentity,source);
 const cell=slice.payload.nativeCells;
 assert.equal(cell.version,native.version);
 const [a,b,c,d]=[cell.lower.x1[0],cell.upper.x1[0],cell.lower.x2[0],cell.upper.x2[0]];
 assert.ok(a>=0&&b>a&&d>c);assert.equal(cell.lower.x3[0],0);assert.equal(cell.upper.x3[0],0);
 const volume=Math.PI*(b*b-a*a)*(d-c),angular=2*Math.PI*(b*b*b-a*a*a)*(d-c)/3;
 // Same 2e-12 geometry budget as the existing independent native IO owner.
 const close=(actual,expected)=>assert.ok(Number.isFinite(actual)&&expected>0&&Math.abs(actual-expected)<=2e-12*expected);
 close(cell.cellMeasure[0],volume);close(cell.angularMeasure[0],angular);
 assert.ok(Number.isFinite(cell.mPhi[0])&&Number.isFinite(cell.angularMomentumDensity[0]));
 const jV=cell.angularMomentumDensity[0]*cell.cellMeasure[0],jW=cell.mPhi[0]*cell.angularMeasure[0];
 if(jW===0)assert.equal(jV,0);else assert.ok(Math.abs(jV-jW)<=2e-12*Math.abs(jW));
 // The formal point query uses recorded intrinsic (r,z) bounds. Both isolated
 // calls pass the same real typed client validators before returning results.
 const point={field:'DENS',point:[(a+b)/2,(c+d)/2]};
 const located=await readPlotfilePointIsolated(path,point);
 assert.equal(located.file.sha256,digest);assert.deepEqual(located.candidateSourceIdentity,source);
 assert.deepEqual(located.payload.values,slice.payload.values);
 assert.deepEqual(located.payload.nativeCells,slice.payload.nativeCells);
 assert.equal(located.pointEvidence.matchCount,1);
 assert.equal(located.pointEvidence.scannedCells,metadata.cells);
 assert.deepEqual(located.pointEvidence.point,point.point);
 const after=await fs.stat(path,{bigint:true});
 for(const key of ['dev','ino','size','mtimeNs','ctimeNs'])assert.equal(after[key],before[key]);
 assert.equal(await hash(path),digest);
 return {status:'PASS_READER_ENGINEERING_ONLY',fileSha256:digest,publication:'complete',nativeMetadata:'accepted',nativeSlice:'accepted',
  fileHashesUnchanged:true,nativePoint:'accepted formal intrinsic (r,z) query; same raw cell as native slice',G:{recordedSeparately:false,
   provenance:'shared CGS constant is part of the authenticated build/source identity; gravity_G is retired'},
  sourceIdentity:source,nativeBounds:[a,b,c,d],volume:cell.cellMeasure[0],angularMeasure:cell.angularMeasure[0],
  FP64:'production header and native slice validation; no independent full-domain HDF oracle',
  scope:'One actual formal native cell, typed writer provenance, read-only slice/point equality; no rendering, evolution or physical qualification'};
}

const summary=formalOnly?{status:'PENDING',scope:'Actual formal-native reader engineering only'}:await inspectHistoricalCandidates();
if(formalOnly||process.argv.length===6){
 summary.formalRz=await inspectFormalRz(formalOnly?process.argv[3]:process.argv[5]);
 summary.status='PASS_READER_ENGINEERING_ONLY';
}
await fs.writeFile(formalOnly?process.argv[4]:process.argv[3],JSON.stringify(summary,null,2)+'\n',{flag:'wx'});
console.log(JSON.stringify(summary,null,2));
