import {useMemo,useRef,useState} from 'react';
import {HeatmapVis,Annotation,useCanvasEvent} from '@h5web/lib';
import ndarray from 'ndarray';
import {sampleEdges} from '../../data/RealInitPreviewProvider';
import type {RealGrid} from '../../data/RealInitPreviewProvider';
function Selection({data,selected,onPoint}:{data:RealGrid;selected:number|null;onPoint:(x:number,y:number)=>void}){
 const down=useRef<{x:number;y:number}|null>(null);
 useCanvasEvent('mousedown',({sourceEvent:e})=>{down.current=e.button===0?{x:e.clientX,y:e.clientY}:null;});
 useCanvasEvent('mouseup',({sourceEvent:e,dataPt})=>{const start=down.current;down.current=null;if(start&&e.button===0&&Math.hypot(e.clientX-start.x,e.clientY-start.y)<4)onPoint(dataPt.x,dataPt.y);});
 return selected===null?null:<Annotation x={data.x[selected%data.width]} y={data.y[Math.floor(selected/data.width)]} center><span className="selected-point" role="img" aria-label="Selected real 2D sample"/></Annotation>;
}
export function RealGridRenderer({data,selected,onPoint}:{data:RealGrid;selected:number|null;onPoint:(x:number,y:number)=>void}){
 const xEdges=useMemo(()=>sampleEdges(data.x),[data.x]);const yEdges=useMemo(()=>sampleEdges(data.y),[data.y]);
 const [fit,setFit]=useState(0);const array=useMemo(()=>ndarray(data.values,[data.height,data.width]),[data]);
 return <><button onClick={()=>setFit(n=>n+1)}>Fit 2D view</button>{data.min===data.max&&<p>Constant field: {data.min.toPrecision(8)}. Color scale padded for display only.</p>}<p className="section-note">Scroll to zoom · drag to pan · click to inspect. x1 right, x2 up. Display samples, not AMR cells.</p><div className="heatmap-renderer" aria-label={`${data.field} real 2D heatmap`}><HeatmapVis key={fit} dataArray={array} domain={[data.min,data.max===data.min?data.min+Math.max(1,Math.abs(data.min)*0.01):data.max]} colorMap="Viridis" abscissaParams={{label:'x1',value:xEdges}} ordinateParams={{label:'x2',value:yEdges}} renderTooltip={()=> <></>}><Selection data={data} selected={selected} onPoint={onPoint}/></HeatmapVis></div></>;
}
