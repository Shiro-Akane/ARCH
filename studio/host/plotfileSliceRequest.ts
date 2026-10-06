/** Pure request boundary; importing this module does not initialize HDF5/WASM. */
export interface PlotfileSliceRequest {field:string;block:number;start:number[];count:number[]}
export const MAX_SLICE_CELLS=512;
export function copyPlotfileSliceRequest(request:unknown):PlotfileSliceRequest{
 if(!request||typeof request!=='object'||Array.isArray(request))throw Error('Invalid slice request.');
 const r=request as Record<string,unknown>;
 if(Object.keys(r).sort().join(',')!=='block,count,field,start'||
    typeof r.field!=='string'||r.field.length<1||r.field.length>128||
    typeof r.block!=='number'||!Number.isSafeInteger(r.block)||r.block<0||
    !Array.isArray(r.start)||!Array.isArray(r.count)||r.start.length<1||r.start.length>3||r.count.length!==r.start.length||
    r.start.some(n=>!Number.isSafeInteger(n)||n<0)||r.count.some(n=>!Number.isSafeInteger(n)||n<1))
  throw Error('Invalid slice request.');
 if(r.count.reduce((a:number,b:number)=>a*b,1)>MAX_SLICE_CELLS)throw Error('Slice exceeds 512-sample budget.');
 return {field:r.field,block:r.block,start:[...r.start],count:[...r.count]};
}
