import type {Range} from '../../data/plotPresentation';
import type {LinePreviewData} from '../../data/LinePreviewData';
import {PhysicalPlot} from './PhysicalPlot';
import type {PositionMarkerProps} from './PhysicalPlot';
export function RealLineRenderer({data,selected,onPoint,marker,domain}:{domain?:Range;data:LinePreviewData;selected:number|null;onPoint:(x:number)=>void;marker?:PositionMarkerProps}){
 return <PhysicalPlot xDomain={domain} key={data.field} x={data.x} values={data.values} field={data.field} selected={selected} onPoint={onPoint} marker={marker}/>;
}
