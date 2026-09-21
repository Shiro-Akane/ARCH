import {useState} from 'react';
import type {ConfigurationSchema,ConfigurationInspection,CoordinateSystem,PathCheck} from '../../host/configurationContracts';
import {catalog,catalogCoordinates,catalogOptions,parameterGroups,parameterUnit} from '../../data/parameterCatalog';
import {ConfigControl} from './ConfigControl';
/** Virtual defaults never enter the document until an explicit user edit. */
export function StandardCatalog({schema,values,inspection,pathChecks,errors,onEdit,onSelect}:{schema:ConfigurationSchema;values:Record<string,string>;inspection:ConfigurationInspection|undefined;pathChecks:PathCheck[]|undefined;errors:Record<string,string>;onEdit:(key:string,value:string)=>void;onSelect:(key:string)=>void}){
 const [group,setGroup]=useState<string>('Grid');const [search,setSearch]=useState('');
 const rows=catalog(schema.parameters,values);
 const coordinates=catalogCoordinates(schema,values);
 const [lastCoordinates,setLastCoordinates]=useState<CoordinateSystem|undefined>(coordinates);
 if(coordinates&&coordinates!==lastCoordinates)setLastCoordinates(coordinates);
 const axes=(coordinates??lastCoordinates)?.axes;
 const query=search.trim().toLowerCase();
 function render(row:typeof rows[number]){
  const {parameter:p,sourceKey,value,explicit,aliases}=row;
  const parsed=inspection?.parameters.find(x=>x.key===p.key);
  const path=pathChecks?.find(x=>x.key===p.key);
  const diagnostics=inspection?.diagnostics.filter(d=>d.parameterKey===p.key)??[];
  return <div className="parameter-field" key={p.key} onFocus={()=>onSelect(sourceKey)} onClick={()=>onSelect(sourceKey)}>
   <span>{p.key}</span>
   <ConfigControl name={p.key} value={value} authoritative meta={{group:'Runtime',type:p.type==='string'?'text':p.type,evidence:'ARCH --config-schema',options:catalogOptions(p)}} error={errors[sourceKey]??errors[p.key]} onChange={v=>onEdit(sourceKey,v)}/>
   <small>{explicit?`Explicit Working Copy · ${sourceKey}`:'Schema Default · not written'}{aliases.length?` · alias: ${aliases.join(', ')}`:''}</small>
   <small>{parameterUnit(p,parsed,inspection?.coordinates)}</small>
   {parsed?.applicable===false&&<small>Not applicable in current inspection. {p.applicability} Explicit text retained.</small>}
   {!!p.options?.unavailableReason&&<small>{String(p.options.unavailableReason)} Unavailable: {String(p.options.unavailableValues)}</small>}
   {!!p.options?.availability&&<small>{String(p.options.availability)}</small>}
   {errors[sourceKey]&&!diagnostics.some(d=>d.message===errors[sourceKey])&&<small className="validation-error">{errors[sourceKey]}</small>}
   {diagnostics.map((d,i)=><small key={i} className={d.severity==='error'?'validation-error':''}>{d.code}: {d.message}</small>)}
   {p.path&&<div className="path-preflight"><small>Host path: {path?`${path.status} · ${path.message}`:'Not checked · current Host inspection unavailable'}</small>{path&&<><small>Core cwd: {path.cwd}</small><small>Resolved: {path.resolvedPath??'Not set'}</small>{path.parent&&<small>Parent: {path.parent}</small>}</>}</div>}
  </div>;
 }
 const axisKeys=new Set(axes?.flatMap(a=>[a.blocksKey,a.minKey,a.maxKey,a.lowerBoundaryKey,a.upperBoundaryKey])??[1,2,3].flatMap(i=>[`nblockx${i}`,`x${i}_min`,`x${i}_max`,`x${i}l_boundary_type`,`x${i}r_boundary_type`]));
 const find=(key:string)=>rows.find(r=>r.parameter.key===key);
 const advanced=(key:string)=>group==='Network'&&!['use_burn','network_name','use_nse'].includes(key);
 const filtered=rows.filter(r=>query?[r.parameter.key,...r.aliases,r.value,r.parameter.group].join(' ').toLowerCase().includes(query):r.parameter.group===group);
 return <section className="standard-catalog" aria-label="Standard parameter catalog">
  <p>{schema.parameters.length} standard keys · {rows.length} controls · aliases share one control</p>
  <input className="parameter-search" aria-label="Search all standard parameters" placeholder="Search all standard parameters…" value={search} onChange={e=>setSearch(e.target.value)}/>
  <nav className="block-navigator" aria-label="Standard parameter groups">{parameterGroups.map(g=><button key={g} aria-pressed={g===group} onClick={()=>{setGroup(g);setSearch('');}}>{g}</button>)}</nav>
  <h3>{query?'Search results':group}</h3>
  {query?filtered.map(render):<>
   {group==='Grid'&&<><p>Dimension: {(coordinates??lastCoordinates)?.dimension??'Unavailable'} · {coordinates?'Core coordinate catalog':'Last valid layout; correct invalid input'} · 0 = off, ≥1 = on; x3 requires x2</p>
    {[1,2,3].map((n,i)=>{const axis=axes?.[i];const key=axis?.blocksKey??`nblockx${n}`;const blocks=find(key);const currentAxis=inspection?.coordinates?.axes[i];return <section className="grid-axis-block" key={n} aria-label={`Grid x${n} axis`}><h4>x{n} · {axis?.displayName??'Coordinate unavailable'} · {axis?.active?'active':'off'}</h4>{blocks&&render(blocks)}{axis?.active&&<><p>{currentAxis?.unit??'Unit unavailable until current inspection'}</p>{[axis.minKey,axis.maxKey,axis.lowerBoundaryKey,axis.upperBoundaryKey].map(k=>{const row=find(k);return row?render(row):null;})}</>}</section>;})}
   </>}
   {filtered.filter(r=>!(group==='Grid'&&axisKeys.has(r.parameter.key))&&!advanced(r.parameter.key)).map(render)}
   {filtered.some(r=>advanced(r.parameter.key))&&<details className="advanced-parameters"><summary>Network ODE / Advanced</summary>{filtered.filter(r=>advanced(r.parameter.key)).map(render)}</details>}
  </>}
 </section>;
}
