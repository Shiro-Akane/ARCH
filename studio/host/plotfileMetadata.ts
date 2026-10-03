/**
 * Inspect the current ARCH writer's structure without loading field arrays.
 * This is a local audit primitive, not a result provider: readable HDF5 and
 * stable file identity do not prove writer completion or scientific provenance.
 */
import {constants} from 'node:fs';
import {open,realpath} from 'node:fs/promises';
import {resolve} from 'node:path';
import {createHash} from 'node:crypto';
import h5 from 'h5wasm/node';

const MAX_FILE_BYTES=64*1024*1024;
const MAX_FIELDS=128;
const MAX_CELLS=10_000_000;

function shapeOf(dataset: unknown, label: string): number[] {
 if(!(dataset instanceof h5.Dataset) || ![0,1].includes(dataset.metadata.type))
  throw new Error('Unsupported numeric dataset: '+label);
 const shape=dataset.shape;
 if(!shape || !shape.length || shape.length>4 || shape.some(n=>!Number.isSafeInteger(n)||n<1))
  throw new Error('Invalid dataset shape: '+label);
 let cells=1;
 for(const n of shape){cells*=n;if(!Number.isSafeInteger(cells)||cells>MAX_CELLS)throw new Error('Dataset exceeds metadata audit budget: '+label);}
 return shape;
}
function scalar(file: InstanceType<typeof h5.File>, name: string): unknown {
 const attribute=file.attrs[name];
 if(!attribute || (attribute.shape && attribute.shape.reduce((a,b)=>a*b,1)!==1))
  throw new Error('Missing or non-scalar plot metadata: '+name);
 // The only string consumed is the writer's bounded geometry identifier.
 if(attribute.metadata.size>128)throw new Error('Oversized plot metadata: '+name);
 const value=attribute.json_value;
 return Array.isArray(value)&&value.length===1?value[0]:value;
}
function group(file: InstanceType<typeof h5.File>, name: string): InstanceType<typeof h5.Group> {
 const entity=file.get(name);
 if(!(entity instanceof h5.Group))throw new Error('Missing plot group: '+name);
 return entity;
}
function dataset(group: InstanceType<typeof h5.Group>, name: string) {
 const value=group.get(name);
 // External links are returned as ExternalLink objects, never dereferenced here.
 if(!(value instanceof h5.Dataset))throw new Error('Missing local dataset: '+name);
 return value;
}

export interface PlotfileSliceRequest {field:string;block:number;start:number[];count:number[]}
const MAX_SLICE_CELLS=512;
type RawNumber=number|'NaN'|'Infinity'|'-Infinity';
function rawNumbers(value:unknown,expected:number):RawNumber[] {
 if(!ArrayBuffer.isView(value)||value instanceof DataView||value instanceof BigInt64Array||value instanceof BigUint64Array)
  throw Error('Unsupported slice numeric representation.');
 const numbers=Array.from(value as unknown as ArrayLike<number>);
 if(numbers.length!==expected)throw Error('Slice payload length mismatch.');
 return numbers.map(n=>Number.isNaN(n)?'NaN':n===Infinity?'Infinity':n===-Infinity?'-Infinity':n);
}
function readSlice(file:InstanceType<typeof h5.File>,shape:number[],request:PlotfileSliceRequest) {
 const {field,block,start,count}=request,cellShape=shape.slice(1);
 if(!Number.isSafeInteger(block)||block<0||block>=shape[0]||start.length!==cellShape.length||count.length!==cellShape.length)
  throw Error('Invalid block or slice dimension.');
 let cells=1;
 for(let axis=0;axis<cellShape.length;axis++){
  if(!Number.isSafeInteger(start[axis])||!Number.isSafeInteger(count[axis])||start[axis]<0||count[axis]<1||start[axis]+count[axis]>cellShape[axis])
   throw Error('Slice bounds outside stored cell shape.');
  cells*=count[axis];if(cells>MAX_SLICE_CELLS)throw Error('Slice exceeds 512-sample budget.');
 }
 const data=dataset(group(file,'Data'),field);
 if(data.metadata.type!==1||![4,8].includes(data.metadata.size))throw Error('Slice requires float32/float64 fields.');
 const values=rawNumbers(data.slice([[block,block+1],...start.map((n,i)=>[n,n+count[i]] as [number,number])]),cells);
 const blockCells=cellShape.reduce((a,b)=>a*b,1),indices:number[]=[];
 for(let n=0;n<cells;n++){
  let local=n,index=0,stride=1;
  for(let axis=cellShape.length-1;axis>=0;axis--){index+=(start[axis]+local%count[axis])*stride;local=Math.floor(local/count[axis]);stride*=cellShape[axis];}
  indices.push(block*blockCells+index);
 }
 const grid=group(file,'Grid'),coordinates:Record<string,RawNumber[]>={};
 for(const axis of ['x','y','z']){
  const coordinate=dataset(grid,axis);
  if(coordinate.metadata.type!==1||![4,8].includes(coordinate.metadata.size))throw Error('Slice requires float32/float64 coordinates.');
  const result:RawNumber[]=[];
  // Each row is contiguous in x1; never read the complete coordinate array.
  for(let n=0;n<cells;n+=count[count.length-1]){
   const length=count[count.length-1],index=indices[n];
   result.push(...rawNumbers(coordinate.slice([[index,index+length]]),length));
  }
  coordinates[axis]=result;
 }
 const nonFinite=values.some(v=>typeof v!=='number')||Object.values(coordinates).some(a=>a.some(v=>typeof v!=='number'));
 return {field,block,start:[...start],shape:[...count],order:'x1-fastest',linearIndices:indices,values,coordinates,
  unit:null,nonFiniteEncoding:'IEEE special values as explicit strings',diagnostics:nonFinite?['NONFINITE_RAW_VALUES']:[]};
}
async function auditPlotfile(path:string,request?:PlotfileSliceRequest) {
 const source=await open(path,constants.O_RDONLY|constants.O_NOFOLLOW|constants.O_NONBLOCK);
 try {
  // Reject parent-directory symlink swaps before HDF5 reads any bytes.
  if(await realpath('/proc/self/fd/'+source.fd)!==resolve(path))
   throw new Error('Plotfile path identity changed during open.');
  const before=await source.stat({bigint:true});
  if(!before.isFile()||before.size<1n||before.size>BigInt(MAX_FILE_BYTES))
   throw new Error('Choose a regular non-empty plotfile of at most 64 MiB for metadata audit.');
  await h5.ready;
  // Pin the descriptor on Linux: replacement of the selected path cannot switch
  // the HDF5 object between stat/hash/header reads.
  const file=new h5.File('/proc/self/fd/'+source.fd,'r');
  let structure;
  let payload:ReturnType<typeof readSlice>|undefined;
  try {
   if(file.file_id<0n)throw new Error('Could not open HDF5 plotfile.');
   const time=scalar(file,'time'), dimension=scalar(file,'dim'), geometry=scalar(file,'geometry');
   if(typeof time!=='number'||!Number.isFinite(time)||time<0||
      ![1,2,3].includes(Number(dimension))||typeof dimension!=='number'||
      typeof geometry!=='string'||!['cartesian','cylindrical','spherical'].includes(geometry))
    throw new Error('Invalid ARCH plotfile metadata.');
   const grid=group(file,'Grid'),data=group(file,'Data');
   const names=data.keys();
   if(!names.length||names.length>MAX_FIELDS)throw new Error('Invalid field count or field audit budget exceeded.');
   const fields=names.map(name=>({name,shape:shapeOf(dataset(data,name),name),unit:null}));
   const shape=fields[0].shape;
   if(shape.length!==dimension+1||fields.some(f=>JSON.stringify(f.shape)!==JSON.stringify(shape)))
    throw new Error('Field shapes must match [blocks, ...cellShape] for the selected dimension.');
   const cells=shape.reduce((a,b)=>a*b,1),blocks=shape[0];
   for(const axis of ['x','y','z']){
    const s=shapeOf(dataset(grid,axis),'Grid/'+axis);
    if(s.length!==1||s[0]!==cells)throw new Error('Coordinate shape mismatch: '+axis);
   }
   for(const name of ['level','morton']){
    const s=shapeOf(dataset(grid,name),'Grid/'+name);
    if(s.length!==1||s[0]!==blocks)throw new Error('Block metadata shape mismatch: '+name);
   }
   if(request){if(!names.includes(request.field))throw Error('Unknown stored field.');payload=readSlice(file,shape,request);}
   structure={time,dimension,geometry,blocks,cellShape:shape.slice(1),cells,order:'x1-fastest',fields,
    coordinates:{storedBasis:'cartesian',centering:'cell-center',units:null},
    completion:{state:'unknown',reason:'Current writer has no authoritative completion marker or atomic publish contract.'},
    scientificIdentity:{case:null,config:null,build:null,binary:null,eos:null},
    nativeCellGeometry:{bounds:'unavailable',volume:'unavailable'},
    renderEligible:false,
    diagnostics:[request?'FIELD_SLICE_AUDIT':'METADATA_ONLY','OUTPUT_COMPLETION_UNVERIFIED','UNITS_UNAVAILABLE','SCIENTIFIC_IDENTITY_UNAVAILABLE','NATIVE_CELL_GEOMETRY_UNAVAILABLE']};
  } finally {if(file.file_id>=0n)file.close();}
  const hash=createHash('sha256'),buffer=Buffer.alloc(64*1024);
  let position=0;
  while(true){const {bytesRead}=await source.read(buffer,0,buffer.length,position);if(!bytesRead)break;hash.update(buffer.subarray(0,bytesRead));position+=bytesRead;if(position>MAX_FILE_BYTES)throw new Error('Plotfile changed beyond audit budget.');}
  const after=await source.stat({bigint:true});
  if(before.size!==after.size||before.mtimeNs!==after.mtimeNs||before.ctimeNs!==after.ctimeNs||position!==Number(before.size))
   throw new Error('Plotfile changed during metadata audit; retry only after authoritative completion.');
  return {schemaVersion:request?'audit-slice-1':'audit-1',payload,file:{bytes:Number(before.size),sha256:hash.digest('hex'),device:before.dev.toString(),inode:before.ino.toString(),mtimeNs:before.mtimeNs.toString(),ctimeNs:before.ctimeNs.toString()},...structure};
 } finally {await source.close();}
}


/** Read only headers and a streaming file digest; never load field payloads. */
export function inspectPlotfileMetadata(path:string){return auditPlotfile(path);}

/** Local audit primitive only. Production use requires isolated worker ownership. */
export function readPlotfileFieldSlice(path:string,request:PlotfileSliceRequest){
 if(!request||Object.keys(request).sort().join(',')!=='block,count,field,start'||
    typeof request.field!=='string'||request.field.length<1||request.field.length>128||
    !Array.isArray(request.start)||!Array.isArray(request.count)||
    request.start.length<1||request.start.length>3||request.count.length!==request.start.length)
  return Promise.reject(Error('Invalid slice request.'));
 return auditPlotfile(path,{field:request.field,block:request.block,start:[...request.start],count:[...request.count]});
}
