/** Candidate display contract; no Core initialization or scientific integration. */
export type PlotfileDomain={x:[number,number];y:[number,number]};
export interface PlotfileOverviewRequest {field:string;width:number;height:number;viewport?:PlotfileDomain}
export const MAX_OVERVIEW_BLOCKS=128;
export interface NativePlotBlock {
 index:number;firstCellIndex:number;level:number;logicalKey:string;logicalCoordinates:number[];
 lower:[number,number,number];upper:[number,number,number];cellShape:[number,number,number];
}
export interface NativePlotBlocks {
 version:'candidate-leaf-outlines-1';identityScope:'file-local';kind:'stored-active-leaf';
 totalBlocks:number;limit:128;complete:boolean;blocks:NativePlotBlock[];
}
export interface PlotfileOverview {
 nativeBlocks?:NativePlotBlocks;
 globalDomain?:PlotfileDomain;
 version:'candidate-overview-1';field:string;width:number;height:number;dimension:1|2;
 domain:{x:[number,number];y:[number,number]};
 values:(number|null)[];representativeIndices:(number|null)[];
 nonfiniteCells:number;scannedCells:number;
 reduction:'coordinate-overlap-weighted-display-mean';
 diagnostics:string[];
}
export function copyOverviewRequest(value:unknown):PlotfileOverviewRequest {
 if(!value||typeof value!=='object'||Array.isArray(value))throw Error('Invalid overview request.');
 const r=value as Record<string,unknown>;
 if(!['field,height,width','field,height,viewport,width'].includes(Object.keys(r).sort().join(','))||typeof r.field!=='string'||!r.field.length||r.field.length>128||
  !Number.isSafeInteger(r.width)||Number(r.width)<1||Number(r.width)>32||
  !Number.isSafeInteger(r.height)||Number(r.height)<1||Number(r.height)>32)
  throw Error('Overview requires a field and 1..32 pixels per axis.');
 const viewport=r.viewport===undefined?undefined:copyPlotfileDomain(r.viewport);
 if('viewport' in r&&!viewport)throw Error('Explicit viewport must contain physical ranges.');
 return {field:r.field,width:Number(r.width),height:Number(r.height),...(viewport?{viewport}:{})};
}
export function copyPlotfileDomain(value:unknown):PlotfileDomain{
 if(!value||typeof value!=='object'||Array.isArray(value))throw Error('Invalid physical viewport.');
 const r=value as Record<string,unknown>;
 if(Object.keys(r).sort().join(',')!=='x,y')throw Error('Viewport accepts only x/y ranges.');
 for(const range of [r.x,r.y])if(!Array.isArray(range)||range.length!==2||
  range.some(v=>typeof v!=='number'||!Number.isFinite(v))||range[1]<=range[0]||!Number.isFinite(range[1]-range[0]))
  throw Error('Viewport ranges must be finite and increasing.');
 return {x:[...(r.x as [number,number])],y:[...(r.y as [number,number])]};
}
/** Streaming accumulator. Weights are display coordinate overlaps, NOT cell_measure. */
export function createOverview(request:PlotfileOverviewRequest,dimension:1|2,domain:PlotfileOverview['domain']){
 const {width,height}=request;
 if(dimension===1&&height!==1)throw Error('1D overview height must be 1.');
 for(const range of [domain.x,...(dimension===2?[domain.y]:[])])
  if(!range.every(Number.isFinite)||range[1]<=range[0]||!Number.isFinite(range[1]-range[0]))throw Error('Invalid overview domain.');
 const count=width*height,sums=new Float64Array(count),weights=new Float64Array(count);
 const largest=new Float64Array(count),invalid=new Uint8Array(count);
 const representativeIndices:(number|null)[]=Array(count).fill(null);
 let nonfiniteCells=0,scannedCells=0;
 const dx=(domain.x[1]-domain.x[0])/width,dy=dimension===2?(domain.y[1]-domain.y[0])/height:1;
 if(!(dx>0)||!(dy>0))throw Error('Overview pixels collapse at this range.');
 function add(index:number,lo:[number,number],hi:[number,number],value:number){
  if(!Number.isSafeInteger(index)||index<0||
   ![lo[0],hi[0],...(dimension===2?[lo[1],hi[1]]:[])].every(Number.isFinite)||
   hi[0]<=lo[0]||dimension===2&&hi[1]<=lo[1])throw Error('Invalid overview cell bounds.');
  scannedCells++;const finite=Number.isFinite(value);if(!finite)nonfiniteCells++;
  const x0=Math.max(0,Math.floor((lo[0]-domain.x[0])/dx)),x1=Math.min(width-1,Math.ceil((hi[0]-domain.x[0])/dx)-1);
  const y0=dimension===2?Math.max(0,Math.floor((lo[1]-domain.y[0])/dy)):0;
  const y1=dimension===2?Math.min(height-1,Math.ceil((hi[1]-domain.y[0])/dy)-1):0;
  for(let j=y0;j<=y1;j++)for(let i=x0;i<=x1;i++){
   const px=domain.x[0]+i*dx,py=domain.y[0]+j*dy;
   const overlap=Math.max(0,Math.min(hi[0],px+dx)-Math.max(lo[0],px))*
    (dimension===2?Math.max(0,Math.min(hi[1],py+dy)-Math.max(lo[1],py)):1);
   if(!overlap)continue;
   const p=j*width+i;
   if(overlap>largest[p]){largest[p]=overlap;representativeIndices[p]=index;}
   if(finite){sums[p]+=value*overlap;weights[p]+=overlap;}else invalid[p]=1;
  }
 }
 function finish():PlotfileOverview{
  const values=Array.from(sums,(s,i)=>invalid[i]||!weights[i]||!Number.isFinite(s/weights[i])?null:s/weights[i]);
  return {version:'candidate-overview-1',...request,dimension,domain,values,representativeIndices,nonfiniteCells,scannedCells,
   reduction:'coordinate-overlap-weighted-display-mean',
   diagnostics:['DISPLAY_LOD_NOT_NATIVE_VALUES','FULL_LEAF_SCAN','UNITS_REVIEW_PENDING',
    ...(nonfiniteCells?['NONFINITE_VALUES_MASKED_EXPLICITLY']:[])]};
 }
 return {add,finish};
}

export function validOverview(value:unknown,request:PlotfileOverviewRequest,total:number,dimension:number,cellShape?:number[],blocks?:number):value is PlotfileOverview{
 if(!value||typeof value!=='object')return false;
 const v=value as PlotfileOverview,n=request.width*request.height;
 return v.version==='candidate-overview-1'&&v.field===request.field&&v.width===request.width&&v.height===request.height&&
  v.dimension===dimension&&(dimension===1?request.height===1:dimension===2)&&
  (request.viewport===undefined?(!v.globalDomain||JSON.stringify(v.domain)===JSON.stringify(v.globalDomain)):
   !!v.globalDomain&&JSON.stringify(v.domain)===JSON.stringify(request.viewport))&&
  (v.globalDomain===undefined||[v.globalDomain.x,v.globalDomain.y].every(a=>Array.isArray(a)&&a.length===2&&
   a.every(Number.isFinite)&&a[1]>a[0]&&Number.isFinite(a[1]-a[0])))&&
  v.reduction==='coordinate-overlap-weighted-display-mean'&&v.scannedCells===total&&
  Number.isSafeInteger(v.nonfiniteCells)&&v.nonfiniteCells>=0&&v.nonfiniteCells<=total&&
  !!v.domain&&[v.domain.x,v.domain.y].every(a=>Array.isArray(a)&&a.length===2&&a.every(Number.isFinite)&&a[1]>a[0]&&Number.isFinite(a[1]-a[0]))&&
  Array.isArray(v.values)&&v.values.length===n&&v.values.every(x=>x===null||typeof x==='number'&&Number.isFinite(x))&&
  Array.isArray(v.representativeIndices)&&v.representativeIndices.length===n&&
  v.representativeIndices.every(x=>x===null||Number.isSafeInteger(x)&&x>=0&&x<total)&&
  Array.isArray(v.diagnostics)&&v.diagnostics.every(x=>typeof x==='string'&&x.length<=128)&&
  v.diagnostics.includes('DISPLAY_LOD_NOT_NATIVE_VALUES')&&v.diagnostics.includes('FULL_LEAF_SCAN')&&
  (v.nativeBlocks===undefined||validNativeBlocks(v.nativeBlocks,total,dimension,v.globalDomain??v.domain)&&
   (blocks===undefined||v.nativeBlocks.totalBlocks===blocks)&&
   (cellShape===undefined||v.nativeBlocks.blocks.every(b=>
    JSON.stringify(b.cellShape)===JSON.stringify([...cellShape].reverse().concat(Array(3-dimension).fill(1))))));
}

/** Validate only emitted file-local leaf records; never claim parent/coarse hierarchy. */
export function validNativeBlocks(value:unknown,total:number,dimension:number,domain:PlotfileOverview['domain']):value is NativePlotBlocks{
 if(!value||typeof value!=='object')return false;
 const v=value as NativePlotBlocks;
 if(v.version!=='candidate-leaf-outlines-1'||v.identityScope!=='file-local'||v.kind!=='stored-active-leaf'||
  v.limit!==MAX_OVERVIEW_BLOCKS||!Number.isSafeInteger(v.totalBlocks)||v.totalBlocks<1||v.totalBlocks>total||
  v.complete!==(v.totalBlocks<=MAX_OVERVIEW_BLOCKS)||!Array.isArray(v.blocks)||
  v.blocks.length!==Math.min(v.totalBlocks,MAX_OVERVIEW_BLOCKS))return false;
 const keys=new Set<string>();
 return v.blocks.every((b,i)=>{
  if(!b||b.index!==i||!Number.isSafeInteger(b.level)||b.level<0||b.level>0xffffffff||
   !Array.isArray(b.logicalCoordinates)||b.logicalCoordinates.length!==3||
   b.logicalCoordinates.some(n=>!Number.isSafeInteger(n)||n<0||n>0xffffffff)||
   b.logicalKey!==[b.level,...b.logicalCoordinates].join('/')||keys.has(b.logicalKey)||
   !Array.isArray(b.cellShape)||b.cellShape.length!==3||
   b.cellShape.some((n,axis)=>!Number.isSafeInteger(n)||(axis<dimension?n<1:n!==1))||
   b.cellShape.reduce((a,n)=>a*n,1)!==total/v.totalBlocks||
   b.firstCellIndex!==i*(total/v.totalBlocks)||
   !Array.isArray(b.lower)||!Array.isArray(b.upper)||b.lower.length!==3||b.upper.length!==3)return false;
  keys.add(b.logicalKey);
  return b.lower.every((lo,axis)=>{
   const hi=b.upper[axis],range=axis===0?domain.x:domain.y;
   return Number.isFinite(lo)&&Number.isFinite(hi)&&
    (axis<dimension?hi>lo&&lo>=range[0]&&hi<=range[1]:lo===0&&hi===0);
  });
 });
}
