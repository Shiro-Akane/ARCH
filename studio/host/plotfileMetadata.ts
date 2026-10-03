/**
 * Inspect the current ARCH writer's structure without loading field arrays.
 * This is a local audit primitive, not a result provider: readable HDF5 and
 * stable file identity do not prove writer completion or scientific provenance.
 */
import {constants} from 'node:fs';
import {open} from 'node:fs/promises';
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

/** Read only headers and a streaming file digest; never return field payloads. */
export async function inspectPlotfileMetadata(path: string) {
 const source=await open(path,constants.O_RDONLY|constants.O_NOFOLLOW|constants.O_NONBLOCK);
 try {
  const before=await source.stat({bigint:true});
  if(!before.isFile()||before.size<1n||before.size>BigInt(MAX_FILE_BYTES))
   throw new Error('Choose a regular non-empty plotfile of at most 64 MiB for metadata audit.');
  await h5.ready;
  // Pin the descriptor on Linux: replacement of the selected path cannot switch
  // the HDF5 object between stat/hash/header reads.
  const file=new h5.File('/proc/self/fd/'+source.fd,'r');
  let structure;
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
   structure={time,dimension,geometry,blocks,cellShape:shape.slice(1),cells,order:'x1-fastest',fields,
    coordinates:{storedBasis:'cartesian',centering:'cell-center',units:null},
    completion:{state:'unknown',reason:'Current writer has no authoritative completion marker or atomic publish contract.'},
    scientificIdentity:{case:null,config:null,build:null,binary:null,eos:null},
    nativeCellGeometry:{bounds:'unavailable',volume:'unavailable'},
    renderEligible:false,
    diagnostics:['METADATA_ONLY','OUTPUT_COMPLETION_UNVERIFIED','UNITS_UNAVAILABLE','SCIENTIFIC_IDENTITY_UNAVAILABLE','NATIVE_CELL_GEOMETRY_UNAVAILABLE']};
  } finally {if(file.file_id>=0n)file.close();}
  const hash=createHash('sha256'),buffer=Buffer.alloc(64*1024);
  let position=0;
  while(true){const {bytesRead}=await source.read(buffer,0,buffer.length,position);if(!bytesRead)break;hash.update(buffer.subarray(0,bytesRead));position+=bytesRead;if(position>MAX_FILE_BYTES)throw new Error('Plotfile changed beyond audit budget.');}
  const after=await source.stat({bigint:true});
  if(before.size!==after.size||before.mtimeNs!==after.mtimeNs||before.ctimeNs!==after.ctimeNs||position!==Number(before.size))
   throw new Error('Plotfile changed during metadata audit; retry only after authoritative completion.');
  return {schemaVersion:'audit-1',file:{bytes:Number(before.size),sha256:hash.digest('hex'),device:before.dev.toString(),inode:before.ino.toString(),mtimeNs:before.mtimeNs.toString()},...structure};
 } finally {await source.close();}
}
