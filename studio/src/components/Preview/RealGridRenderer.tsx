import type {AmrOverlayProps} from './AmrOverlay';
import type {Range} from '../../data/plotPresentation';
import type {RealGrid} from '../../data/RealInitPreviewProvider';
import {PhysicalPlot} from './PhysicalPlot';
export function RealGridRenderer({data,selected,onPoint,xDomain,yDomain,amr}:{amr?:AmrOverlayProps;xDomain?:Range;yDomain?:Range;data:RealGrid;selected:number|null;onPoint:(x:number,y:number)=>void}){
 return <PhysicalPlot amr={amr} xDomain={xDomain} yDomain={yDomain} key={data.field} x={data.x} y={data.y} values={data.values} field={data.field} selected={selected} onPoint={onPoint}/>;
}
