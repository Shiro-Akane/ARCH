import {useEffect,useRef,useState} from 'react';
import type { CoreGroup } from '../../data/parSchema';
import { coreBlocks } from './panelPresentation';
export function BlockNavigator({selected,onSelect,summary}: {selected:CoreGroup;onSelect:(group:CoreGroup)=>void;summary:(group:CoreGroup)=>string}) {
 const ref=useRef<HTMLElement>(null);const [compact,setCompact]=useState(false);
 useEffect(()=>{const nav=ref.current;const scroll=nav?.closest('.parameter-scroll');if(!nav||!scroll)return;const update=()=>setCompact(scroll.scrollTop>180);scroll.addEventListener('scroll',update,{passive:true});update();return()=>scroll.removeEventListener('scroll',update);},[]);
 return <nav ref={ref} className={`block-navigator${compact?' compact':''}`} aria-label="Core parameter blocks">{coreBlocks.map(group=><button key={group} type="button" aria-pressed={selected===group} onClick={()=>onSelect(group)}><strong>{group}</strong><span>{summary(group)}</span></button>)}</nav>;
}
