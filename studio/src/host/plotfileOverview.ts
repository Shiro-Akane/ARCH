/** Candidate display contract; no Core initialization or scientific integration. */
export interface PlotfileOverviewRequest {field:string;width:number;height:number}
export interface PlotfileOverview {
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
 if(Object.keys(r).sort().join(',')!=='field,height,width'||typeof r.field!=='string'||!r.field.length||r.field.length>128||
  !Number.isSafeInteger(r.width)||Number(r.width)<1||Number(r.width)>32||
  !Number.isSafeInteger(r.height)||Number(r.height)<1||Number(r.height)>32)
  throw Error('Overview requires a field and 1..32 pixels per axis.');
 return {field:r.field,width:Number(r.width),height:Number(r.height)};
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

export function validOverview(value:unknown,request:PlotfileOverviewRequest,total:number,dimension:number):value is PlotfileOverview{
 if(!value||typeof value!=='object')return false;
 const v=value as PlotfileOverview,n=request.width*request.height;
 return v.version==='candidate-overview-1'&&v.field===request.field&&v.width===request.width&&v.height===request.height&&
  v.dimension===dimension&&(dimension===1?request.height===1:dimension===2)&&
  v.reduction==='coordinate-overlap-weighted-display-mean'&&v.scannedCells===total&&
  Number.isSafeInteger(v.nonfiniteCells)&&v.nonfiniteCells>=0&&v.nonfiniteCells<=total&&
  !!v.domain&&[v.domain.x,v.domain.y].every(a=>Array.isArray(a)&&a.length===2&&a.every(Number.isFinite)&&a[1]>a[0]&&Number.isFinite(a[1]-a[0]))&&
  Array.isArray(v.values)&&v.values.length===n&&v.values.every(x=>x===null||typeof x==='number'&&Number.isFinite(x))&&
  Array.isArray(v.representativeIndices)&&v.representativeIndices.length===n&&
  v.representativeIndices.every(x=>x===null||Number.isSafeInteger(x)&&x>=0&&x<total)&&
  Array.isArray(v.diagnostics)&&v.diagnostics.every(x=>typeof x==='string'&&x.length<=128)&&
  v.diagnostics.includes('DISPLAY_LOD_NOT_NATIVE_VALUES')&&v.diagnostics.includes('FULL_LEAF_SCAN');
}
