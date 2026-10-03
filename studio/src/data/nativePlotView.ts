import type {NativePlotCells,RawPlotNumber} from '../host/plotfileAudit.ts';

export type PlotView={x:[number,number];y:[number,number]};
export function finitePlotRange(values:RawPlotNumber[]):[number,number]|null {
 const finite=values.filter((v):v is number=>typeof v==='number'&&Number.isFinite(v));
 if(!finite.length)return null;
 const lo=Math.min(...finite),hi=Math.max(...finite);
 if(lo!==hi)return Number.isFinite(hi-lo)?[lo,hi]:null;
 const pad=Math.abs(lo)*.05||.5;
 return Number.isFinite(lo-pad)&&Number.isFinite(hi+pad)?[lo-pad,hi+pad]:null;
}
export function nativePlotDomain(native:NativePlotCells,dimension:number,values:RawPlotNumber[]):PlotView|null {
 if(dimension!==1&&dimension!==2)return null;
 const x:[number,number]=[Math.min(...native.lower.x1),Math.max(...native.upper.x1)];
 const y=dimension===2?[Math.min(...native.lower.x2),Math.max(...native.upper.x2)] as [number,number]:finitePlotRange(values);
 return y&&validRange(x)&&validRange(y)?{x,y}:null;
}
function validRange(r:[number,number]){
 return r.every(Number.isFinite)&&r[1]>r[0]&&Number.isFinite(r[1]-r[0]);
}
/** Fractions use x rightward and y upward, shared by drawing and picking. */
export function plotFraction(value:number,range:[number,number]){return (value-range[0])/(range[1]-range[0]);}
export function plotValue(fraction:number,range:[number,number]){return range[0]+fraction*(range[1]-range[0]);}
export function zoomPlotView(view:PlotView,factor:number,x=.5,y=.5):PlotView {
 if(!Number.isFinite(factor)||factor<=0)return view;
 const scale=(r:[number,number],f:number):[number,number]=>{
  const anchor=plotValue(f,r);return [anchor+(r[0]-anchor)*factor,anchor+(r[1]-anchor)*factor];
 };
 const result={x:scale(view.x,x),y:scale(view.y,y)};
 return validRange(result.x)&&validRange(result.y)?result:view;
}
export function panPlotView(view:PlotView,dx:number,dy:number):PlotView {
 const move=(r:[number,number],f:number):[number,number]=>{
  const shift=f*(r[1]-r[0]);return [r[0]-shift,r[1]-shift];
 };
 const result={x:move(view.x,dx),y:move(view.y,dy)};
 return validRange(result.x)&&validRange(result.y)?result:view;
}
/** Boundary ties choose the first stored row; gaps never invent a cell. */
export function pickNativePlotCell(native:NativePlotCells,dimension:number,x:number,y:number):number|null {
 if(!Number.isFinite(x)||!Number.isFinite(y))return null;
 const index=native.lower.x1.findIndex((lo,i)=>x>=lo&&x<=native.upper.x1[i]&&
  (dimension===1||y>=native.lower.x2[i]&&y<=native.upper.x2[i]));
 return index<0?null:index;
}
