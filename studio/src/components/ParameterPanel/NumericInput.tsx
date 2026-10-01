import {useRef} from 'react';
import {scrubValue} from './scrub';
export function NumericInput({name,value,onChange,integer=false,range,error}:{name:string;value:string;onChange:(v:string)=>void;integer?:boolean;range?:readonly [number,number];error?:string}) {
 const drag=useRef<{x:number;raw:string;id:number;active:boolean}|null>(null);
 return <input data-numeric="true" aria-label={name} aria-invalid={!!error} inputMode="decimal" value={value} title="Type an exact value, or drag horizontally. Shift: fine adjustment. Esc: cancel drag." onChange={e=>onChange(e.target.value)}
 onPointerDown={e=>{if(e.button!==0)return;drag.current={x:e.clientX,raw:value,id:e.pointerId,active:false};e.currentTarget.setPointerCapture(e.pointerId);}}
 onPointerMove={e=>{const d=drag.current;if(!d)return;const dx=e.clientX-d.x;if(Math.abs(dx)>=4)d.active=true;if(d.active){e.preventDefault();onChange(scrubValue(d.raw,dx,e.shiftKey,integer,range));}}}
 onPointerUp={e=>{drag.current=null;if(e.currentTarget.hasPointerCapture(e.pointerId))e.currentTarget.releasePointerCapture(e.pointerId);}}
 onPointerCancel={()=>{const d=drag.current;drag.current=null;if(d)onChange(d.raw);}}
 onKeyDown={e=>{if(e.key==='Escape'&&drag.current){onChange(drag.current.raw);drag.current=null;e.preventDefault();}if(e.key==='ArrowUp'||e.key==='ArrowDown')e.preventDefault();}} />;
}
