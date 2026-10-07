/** Intrinsic x1/x2/x3 in recorded native axes, never world Cartesian projection.
 * Workflow: copy the finite request, validate complete bounded-scan evidence,
 * then check the single returned cell against the same half-open native rule.
 * This only identifies recorded raw cells; it grants no interpolation, render
 * capability, nearest-cell fallback or numerical/physical qualification.
 */
export interface PlotfilePointRequest {field:string;point:number[]}
export const POINT_BOUNDARY_RULE='half-open; global-maximum-inclusive' as const;
export interface PlotfilePointEvidence {
 version:'candidate-native-point-1';field:string;point:number[];rule:typeof POINT_BOUNDARY_RULE;
 domain:{x:[number,number];y:[number,number];z?:[number,number]};scannedCells:number;matchCount:1;
}
/** Copy only a stored-field name and one/two/three finite intrinsic coordinates. */
export function copyPointRequest(value:unknown):PlotfilePointRequest{
 if(!value||typeof value!=='object'||Array.isArray(value))throw Error('Invalid native point request.');
 const r=value as Record<string,unknown>;
 if(Object.keys(r).sort().join(',')!=='field,point'||typeof r.field!=='string'||!r.field.length||r.field.length>128||
  !Array.isArray(r.point)||![1,2,3].includes(r.point.length)||r.point.some(x=>typeof x!=='number'||!Number.isFinite(x)))
  throw Error('Native point needs a stored field and finite x1[/x2[/x3]] coordinates.');
 return {field:r.field,point:[...r.point]};
}
/** Shared interval ownership: upper edges belong only to the global maximum. */
export function nativeAxisContains(point:number,lo:number,hi:number,globalMaximum:number){
 return point>=lo&&(point<hi||point===hi&&hi===globalMaximum);
}
/** Validate dimensional scan evidence; z is mandatory only for a 3D query.
 * Older 1D/2D responses keep x/y and their existing inactive-y convention.
 */
export function validPointEvidence(value:unknown,request:PlotfilePointRequest,total:number,dimension:number):value is PlotfilePointEvidence {
 if(!value||typeof value!=='object'||Array.isArray(value)||![1,2,3].includes(dimension))return false;
 const v=value as PlotfilePointEvidence;
 if(!v.domain||typeof v.domain!=='object'||Array.isArray(v.domain)||
  Object.keys(v.domain).sort().join(',')!==(dimension===3?'x,y,z':'x,y'))return false;
 const axes=dimension===3?[v.domain.x,v.domain.y,v.domain.z]:[v.domain.x,v.domain.y];
 return v.version==='candidate-native-point-1'&&v.field===request.field&&v.rule===POINT_BOUNDARY_RULE&&
  v.matchCount===1&&v.scannedCells===total&&request.point.length===dimension&&
  JSON.stringify(v.point)===JSON.stringify(request.point)&&
  axes.every(a=>Array.isArray(a)&&a.length===2&&a.every(x=>typeof x==='number'&&Number.isFinite(x))&&a[1]>a[0]&&Number.isFinite(a[1]-a[0]));
}

/** Match the returned cell's intrinsic bounds, without using Cartesian centers. */
export function pointMatchesNativeCell(native:unknown,request:PlotfilePointRequest,evidence:PlotfilePointEvidence){
 if(!native||typeof native!=='object')return false;
 const n=native as {lower?:Record<string,unknown>;upper?:Record<string,unknown>};
 const maxima=[evidence.domain.x[1],evidence.domain.y[1],evidence.domain.z?.[1]];
 return request.point.every((point,axis)=>{
  const name='x'+(axis+1),lo=n.lower?.[name],hi=n.upper?.[name],maximum=maxima[axis];
  return Array.isArray(lo)&&lo.length===1&&Array.isArray(hi)&&hi.length===1&&
   typeof lo[0]==='number'&&Number.isFinite(lo[0])&&typeof hi[0]==='number'&&Number.isFinite(hi[0])&&
   typeof maximum==='number'&&hi[0]>lo[0]&&nativeAxisContains(point,lo[0],hi[0],maximum);
 });
}
