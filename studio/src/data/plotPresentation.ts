/** Display-only projection; never receives configuration or Core request state. */
export type Scale = 'linear' | 'log';
export type Range = [number, number];
export interface AxisSetting {scale:Scale;manual:boolean;min:string;max:string}
export interface FieldSetting extends AxisSetting {lower:boolean;upper:boolean;low:string;high:string;map:'Viridis'|'Hot'}
export const axisDefault=():AxisSetting=>({scale:'linear',manual:false,min:'',max:''});
export const fieldDefault=():FieldSetting=>({...axisDefault(),lower:false,upper:false,low:'',high:'',map:'Viridis'});
export function parseDisplayNumber(s:string){return s.trim()!==''&&/^[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:e[+-]?\d+)?$/i.test(s.trim())?Number(s):NaN;}
export function displayRange(setting:AxisSetting,automatic:Range):Range {
 const r:Range=setting.manual?[parseDisplayNumber(setting.min),parseDisplayNumber(setting.max)]:[...automatic];
 if(!r.every(Number.isFinite)||r[0]>=r[1])throw Error('Range requires finite min < max (original physical values).');
 if(setting.scale==='log'&&r[0]<=0)throw Error('Log requires a positive range. Use a positive Manual minimum or return to Linear.');
 return r;
}
export function fieldRange(s:FieldSetting,automatic:Range):Range {
 const r=displayRange(s,automatic);
 if(s.lower)r[0]=parseDisplayNumber(s.low);
 if(s.upper)r[1]=parseDisplayNumber(s.high);
 if(!r.every(Number.isFinite)||r[0]>=r[1]||(s.scale==='log'&&r[0]<=0))throw Error('Clipping requires finite lower < upper and positive bounds for Log.');
 return r;
}
export function projection(domain:Range,scale:Scale,view:Range=[0,1]){
 const transform=(v:number)=>scale==='log'?Math.log10(v):v;
 const inverse=(v:number)=>scale==='log'?10**v:v;
 const lo=transform(domain[0]),span=transform(domain[1])-lo;
 if(!Number.isFinite(lo)||!Number.isFinite(span)||span<=0)throw Error('Display range cannot be represented at this scale. Choose closer finite bounds.');
 return {forward:(v:number)=>((transform(v)-lo)/span-view[0])/(view[1]-view[0]),inverse:(fraction:number)=>inverse(lo+(view[0]+fraction*(view[1]-view[0]))*span)};
}
function bounded(a:number,b:number):Range {const span=Math.min(1,b-a);const start=Math.max(0,Math.min(1-span,a));return [start,start+span];}
export function zoomView(view:Range,anchor:number,factor:number):Range {const span=Math.max(1e-6,Math.min(1,(view[1]-view[0])*factor));const fixed=view[0]+anchor*(view[1]-view[0]);return bounded(fixed-anchor*span,fixed+(1-anchor)*span);}
export function panView(view:Range,fraction:number):Range {const delta=fraction*(view[1]-view[0]);return bounded(view[0]-delta,view[1]-delta);}
export function paddedRange(min:number,max:number):Range {if(min!==max)return [min,max];if(min>0)return [min*.95,min*1.05];const pad=Math.max(1,Math.abs(min)*.05);return [min-pad,max+pad];}
export function logDataError(values:ArrayLike<number>,scale:Scale,label:string){if(scale==='log')for(let i=0;i<values.length;i++)if(values[i]<=0)return `${label}: Log cannot display zero or negative values (sample ${i}: ${values[i]}). Return to Linear. Raw data is unchanged.`;return null;}

export function previewCoordinateDomain(state:Record<string,unknown>|null|undefined,name:string):Range|undefined {
 const grid=state?.grid;if(!grid||typeof grid!=='object'||!('axes' in grid)||!Array.isArray(grid.axes))return;
 const axis=grid.axes.find((a:unknown)=>a&&typeof a==='object'&&'name' in a&&a.name===name);
 if(axis&&typeof axis.min==='number'&&typeof axis.max==='number'&&Number.isFinite(axis.min)&&Number.isFinite(axis.max)&&axis.min<axis.max)return [axis.min,axis.max];
}
