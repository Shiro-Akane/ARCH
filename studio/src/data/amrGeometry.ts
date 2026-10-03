import type {AmrMesh,AmrLeaf} from '../host/workflowContracts.ts';
export function amrAt(mesh:AmrMesh,x:number,y:number,levels:ReadonlySet<number>,slice?:AmrSlice):AmrLeaf|undefined{
 if(!Number.isFinite(x)||!Number.isFinite(y))return;
 const axes=amrPlaneAxes(mesh,slice);if(!axes.length)return;
 return amrPlaneLeaves(mesh,slice).find(l=>levels.has(l.level)&&x>=l.lower[axes[0]]&&x<=l.upper[axes[0]]&&(axes.length===1||y>=l.lower[axes[1]]&&y<=l.upper[axes[1]]));
}
export function meshDomain(mesh:AmrMesh,axis:number):[number,number]|undefined{
 if(mesh.snapshot==='none'||!mesh.leaves.length||!Number.isInteger(axis)||axis<0||axis>=mesh.dimension)return;
 return [Math.min(...mesh.leaves.map(l=>l.lower[axis])),Math.max(...mesh.leaves.map(l=>l.upper[axis]))];
}
/** Skip visually unresolved cell lines, then visit only the visible native coordinate interval. */
export function visibleCellCoordinates(leaf:AmrLeaf,axis:number,range:[number,number],pixelSpan:number):number[]{
 const min=leaf.lower[axis],max=leaf.upper[axis],step=leaf.cellSpacing[axis],n=leaf.cellShape[axis];
 if(step/(range[1]-range[0])*pixelSpan<6)return [];
 const first=Math.max(1,Math.ceil((Math.max(min,range[0])-min)/step));
 const last=Math.min(n-1,Math.floor((Math.min(max,range[1])-min)/step));
 if(last-first>4096)return []; // drawing-only guard; never changes hierarchy or Inspector.
 return Array.from({length:Math.max(0,last-first+1)},(_,j)=>min+(first+j)*step);
}
export const amrLevelColor=(level:number)=>['#ffbf66','#77d7ff','#ef99e9','#a6e58a','#ff8f92','#d7c1ff'][level%6];


export interface AmrSlice {axis:number;coordinate:number}
/** Return actual leaves intersecting the native coordinate plane, never a fabricated 2D mesh. */
export function amrPlaneAxes(mesh:AmrMesh,slice?:AmrSlice):number[]{
 if(mesh.dimension<3)return Array.from({length:mesh.dimension},(_,axis)=>axis);
 if(!slice||!Number.isInteger(slice.axis)||slice.axis<0||slice.axis>2||!Number.isFinite(slice.coordinate))return [];
 return [0,1,2].filter(axis=>axis!==slice.axis);
}
export function amrPlaneLeaves(mesh:AmrMesh,slice?:AmrSlice):AmrLeaf[]{
 if(mesh.dimension<3)return mesh.leaves;
 if(!amrPlaneAxes(mesh,slice).length||!slice)return [];
 const domain=meshDomain(mesh,slice.axis);if(!domain||slice.coordinate<domain[0]||slice.coordinate>domain[1])return [];
 return mesh.leaves.filter(l=>slice.coordinate>=l.lower[slice.axis]
  &&(slice.coordinate<l.upper[slice.axis]||slice.coordinate===domain[1]&&l.upper[slice.axis]===domain[1]));
}
export function amrAxisLabel(mesh:AmrMesh,axis:number){
 return mesh.coordinates?.metadata.axes[axis]?.displayName??'x'+(axis+1);
}
export function amrAxisUnit(mesh:AmrMesh,axis:number){
 return mesh.coordinates?.metadata.axes[axis]?mesh.coordinates.metadata.axes[axis].unit:mesh.unit;
}
