import type {AmrMesh,AmrLeaf} from '../host/workflowContracts.ts';
export function amrAt(mesh:AmrMesh,x:number,y:number,levels:ReadonlySet<number>):AmrLeaf|undefined{
 if(!Number.isFinite(x)||!Number.isFinite(y))return;
 return mesh.leaves.find(l=>levels.has(l.level)&&x>=l.lower[0]&&x<=l.upper[0]&&(mesh.dimension===1||y>=l.lower[1]&&y<=l.upper[1]));
}
export function meshDomain(mesh:AmrMesh,axis:number):[number,number]|undefined{
 if(mesh.snapshot==='none'||!mesh.leaves.length||axis>=mesh.dimension)return;
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
