import type { ReactNode } from 'react';
import type { ParEntry } from '../../data/ParDocument';
const pairs: [string,string,string][] = [
  ['x1_min','x1_max','X domain'],['x2_min','x2_max','Y domain'],['x3_min','x3_max','Z domain'],
  ['x1l_boundary_type','x1r_boundary_type','X boundary'],['x2l_boundary_type','x2r_boundary_type','Y boundary'],['x3l_boundary_type','x3r_boundary_type','Z boundary'],
  ['lrefinemin','lrefinemax','Refinement levels'],['min_eint','max_eint','Internal energy bounds'],
];
export function PairedFields({entries,renderEntry}: {entries:ParEntry[];renderEntry:(entry:ParEntry)=>ReactNode}) {
 const used=new Set<string>();
 return <>{entries.map(entry=>{
  if(used.has(entry.key))return null;
  const pair=pairs.find(([a,b])=>a===entry.key || b===entry.key);
  if(!pair)return renderEntry(entry);
  used.add(pair[0]);used.add(pair[1]);
  return <fieldset className="compact-pair" key={pair[0]}><legend>{pair[2]}</legend>{pair.slice(0,2).map(key=>{
   const item=entries.find(e=>e.key===key);return item ? renderEntry(item) : <span className="missing-parameter" key={key}>Not specified</span>;
  })}</fieldset>;
 })}</>;
}
