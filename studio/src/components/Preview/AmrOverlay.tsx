import type {AmrMesh} from '../../host/workflowContracts';
import {amrLevelColor,visibleCellCoordinates} from '../../data/amrGeometry';
export interface AmrOverlayProps {mesh:AmrMesh;levels:ReadonlySet<number>;cells:boolean;selected:string|null;selecting:boolean;onSelect:(key:string)=>void}
interface Projection {forward:(v:number)=>number;inverse:(v:number)=>number}
export function AmrOverlay({amr,xp,yp,left,top,width,height}:{amr:AmrOverlayProps;xp:Projection;yp:Projection;left:number;top:number;width:number;height:number}){
 const levels=[...amr.levels].sort((a,b)=>a-b);
 const xr:[number,number]=[xp.inverse(0),xp.inverse(1)],yr:[number,number]=[yp.inverse(0),yp.inverse(1)];
 return <g aria-label="Actual initial AMR hierarchy" pointerEvents="none">{amr.mesh.leaves.filter(l=>amr.levels.has(l.level)).map(l=>{
  const x0=left+xp.forward(l.lower[0])*width,x1=left+xp.forward(l.upper[0])*width;
  const y0=amr.mesh.dimension===2?top+(1-yp.forward(l.upper[1]))*height:top+height-18-levels.indexOf(l.level)*16;
  const y1=amr.mesh.dimension===2?top+(1-yp.forward(l.lower[1]))*height:y0+12;
  if(![x0,x1,y0,y1].every(Number.isFinite)||x1<left||x0>left+width||y1<top||y0>top+height)return null;
  const color=amrLevelColor(l.level);
  return <g key={l.logicalKey} aria-label={'AMR block '+l.logicalKey}><rect x={x0} y={y0} width={x1-x0} height={y1-y0} fill={amr.selected===l.logicalKey?'#ffffff22':'none'} stroke={amr.selected===l.logicalKey?'white':color} strokeWidth={amr.selected===l.logicalKey?3:2}/>{amr.cells&&<g stroke={color} strokeWidth={.6} opacity={.75}>{visibleCellCoordinates(l,0,xr,width).map(x=><line key={'x'+x} x1={left+xp.forward(x)*width} x2={left+xp.forward(x)*width} y1={y0} y2={y1}/>)}{amr.mesh.dimension===2&&visibleCellCoordinates(l,1,yr,height).map(y=><line key={'y'+y} x1={x0} x2={x1} y1={top+(1-yp.forward(y))*height} y2={top+(1-yp.forward(y))*height}/>)}</g>}</g>;
 })}</g>;
}
