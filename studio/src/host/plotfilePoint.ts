/** Physical coordinates in stored native axes; no interpolation or nearest-cell fallback. */
export interface PlotfilePointRequest {field:string;point:number[]}
export const POINT_BOUNDARY_RULE='half-open; global-maximum-inclusive' as const;
export interface PlotfilePointEvidence {
 version:'candidate-native-point-1';field:string;point:number[];rule:typeof POINT_BOUNDARY_RULE;
 domain:{x:[number,number];y:[number,number]};scannedCells:number;matchCount:1;
}
export function copyPointRequest(value:unknown):PlotfilePointRequest{
 if(!value||typeof value!=='object'||Array.isArray(value))throw Error('Invalid native point request.');
 const r=value as Record<string,unknown>;
 if(Object.keys(r).sort().join(',')!=='field,point'||typeof r.field!=='string'||!r.field.length||r.field.length>128||
  !Array.isArray(r.point)||![1,2].includes(r.point.length)||r.point.some(x=>typeof x!=='number'||!Number.isFinite(x)))
  throw Error('Native point needs a stored field and finite x1[/x2] coordinates.');
 return {field:r.field,point:[...r.point]};
}
export function nativeAxisContains(point:number,lo:number,hi:number,globalMaximum:number){
 return point>=lo&&(point<hi||point===hi&&hi===globalMaximum);
}
export function validPointEvidence(value:unknown,request:PlotfilePointRequest,total:number,dimension:number):value is PlotfilePointEvidence {
 if(!value||typeof value!=='object')return false;
 const v=value as PlotfilePointEvidence;
 return v.version==='candidate-native-point-1'&&v.field===request.field&&v.rule===POINT_BOUNDARY_RULE&&
  v.matchCount===1&&v.scannedCells===total&&request.point.length===dimension&&
  JSON.stringify(v.point)===JSON.stringify(request.point)&&
  !!v.domain&&[v.domain.x,v.domain.y].every(a=>Array.isArray(a)&&a.length===2&&a.every(Number.isFinite)&&a[1]>a[0]&&Number.isFinite(a[1]-a[0]));
}

export function pointMatchesNativeCell(native:unknown,request:PlotfilePointRequest,evidence:PlotfilePointEvidence){
 if(!native||typeof native!=='object')return false;
 const n=native as {lower?:Record<string,unknown>;upper?:Record<string,unknown>};
 return request.point.every((point,axis)=>{
  const name='x'+(axis+1),lo=n.lower?.[name],hi=n.upper?.[name];
  return Array.isArray(lo)&&lo.length===1&&Array.isArray(hi)&&hi.length===1&&
   typeof lo[0]==='number'&&Number.isFinite(lo[0])&&typeof hi[0]==='number'&&Number.isFinite(hi[0])&&
   hi[0]>lo[0]&&nativeAxisContains(point,lo[0],hi[0],axis===0?evidence.domain.x[1]:evidence.domain.y[1]);
 });
}
