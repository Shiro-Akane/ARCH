import {useState} from 'react';
import type {ConfigurationSchema,ConfigurationInspection,CoordinateSystem,PathCheck} from '../../host/configurationContracts';
import {catalog,catalogCoordinates,catalogOptions,parameterGroups,parameterUnit} from '../../data/parameterCatalog';
import {forbiddenParameters,parameterPlacement,runtimeOrder,runtimeSection} from '../../data/parameterLayout';
import {ConfigControl} from './ConfigControl';
/** Layout never inserts defaults, deletes hidden text, or interprets physical formulas. */
export function StandardCatalog({schema,values,inspection,pathChecks,errors,onEdit,onSelect,onRemove}:{schema:ConfigurationSchema;values:Record<string,string>;inspection:ConfigurationInspection|undefined;pathChecks:PathCheck[]|undefined;errors:Record<string,string>;onEdit:(key:string,value:string)=>void;onSelect:(key:string)=>void;onRemove?:(key:string)=>void}){
 const [expanded,setExpanded]=useState<Record<string,boolean>>({Grid:true});
 const [search,setSearch]=useState('');
 const rows=catalog(schema.parameters,values),coordinates=catalogCoordinates(schema,values);
 const [lastCoordinates,setLastCoordinates]=useState<CoordinateSystem|undefined>(coordinates);
 if(coordinates&&coordinates!==lastCoordinates)setLastCoordinates(coordinates);
 const layout=coordinates??lastCoordinates,axes=layout?.axes;
 const query=search.trim().toLowerCase(),forbidden=forbiddenParameters(inspection);
 const groups=[...parameterGroups,...new Set(rows.map(r=>r.parameter.group).filter(g=>!(parameterGroups as readonly string[]).includes(g)))];
 const matches=(row:typeof rows[number])=>[row.parameter.key,...row.aliases,row.parameter.presentation?.displayName,row.parameter.presentation?.description,row.parameter.group].join(' ').toLowerCase().includes(query);
 const find=(key:string)=>rows.find(r=>r.parameter.key===key);
 function render(row:typeof rows[number]){
  const {parameter:p,sourceKey,value,explicit,aliases}=row;
  const parsed=inspection?.parameters.find(x=>x.key===p.key),path=pathChecks?.find(x=>x.key===p.key);
  const diagnostics=inspection?.diagnostics.filter(d=>d.parameterKey===p.key)??[];
  const unit=parameterUnit(p,parsed,inspection?.coordinates),toggle=p.presentation?.toggle;
  const unavailable=forbidden.has(p.key);
  const coefficientBlocked=false; // Unknown applicability must not prevent filling required input.
  const labels=Object.fromEntries(Array.isArray(p.options?.choices)?p.options.choices.flatMap(c=>c&&typeof c==='object'&&typeof c.value==='string'&&typeof c.displayName==='string'?[[c.value,c.displayName]]:[]):[]);
  const choices=p.options?.choices;
  const choice=Array.isArray(choices)?choices.find(c=>c&&typeof c==='object'&&Array.isArray(c.acceptedNames)&&c.acceptedNames.some((n:unknown)=>typeof n==='string'&&(p.options?.caseSensitive===false?n.toLowerCase()===value.toLowerCase():n===value))):undefined;
  const shown=choice&&typeof choice.value==='string'?choice.value:value;
  return <div className="parameter-field" key={p.key} data-standard-key={p.key} onFocus={()=>onSelect(sourceKey)}>
   <span className="parameter-label">{p.presentation?.displayName??p.key.replaceAll('_',' ')}</span>
   <small className="parameter-key">{p.key}</small>
   {toggle&&<label className="parameter-toggle"><input type="checkbox" aria-label={p.key+' enabled'} checked={Number(value)>0} onChange={e=>onEdit(sourceKey,e.target.checked?'':String(toggle.offValue))}/>Enabled</label>}
   <fieldset className="parameter-control" disabled={unavailable||coefficientBlocked}>
    <ConfigControl optionLabels={labels} name={p.key} value={shown} authoritative meta={{group:'Runtime',type:p.type==='string'?'text':p.type,evidence:'ARCH --config-schema',options:catalogOptions(p)}} error={errors[sourceKey]??errors[p.key]} onChange={v=>onEdit(sourceKey,v)}/>
   </fieldset>
   {toggle&&value===''&&<small>Enter a positive value to enable; no value is invented.</small>}
   {parsed?.applicability.state==='unknown-dependency'&&<small>Applicability unresolved: {parsed.applicability.missingDependencies.join(', ')}</small>}
   {unit&&<small className="parameter-unit">{unit}</small>}
   {p.presentation?.description&&<p className="parameter-description">{p.presentation.description}</p>}
   <small>{explicit?'Explicit Working Copy':'Missing from Working Copy · not written'}</small>
   {parsed?.applicability.state==='not-applicable'&&<small>Not applicable in current inspection. Existing text retained.</small>}
   {errors[sourceKey]&&<small className="validation-error">{errors[sourceKey]}</small>}
   {diagnostics.map((d,i)=><small key={i} className={d.severity==='error'?'validation-error':''}>{d.code}: {d.message}</small>)}
   {unavailable&&explicit&&onRemove&&<button type="button" onClick={()=>onRemove(sourceKey)}>Remove forbidden parameter {sourceKey}</button>}
   {coefficientBlocked&&!unavailable&&<small>Constant coefficient editing requires matching Core permission; existing text retained.</small>}
   {unavailable&&<small>Core forbids this explicit parameter in the current configuration. Removal is one Undo-able edit; Save remains explicit.</small>}
   <details className="parameter-help"><summary>Details · {p.key}</summary>
    <p>Type: {p.type} · Schema Default: {p.allowedDefault?String(p.allowedDefault.value):'None permitted'} · Source: {p.allowedDefault?.source??'No default'}</p>
    <p>Working token: {explicit?value:'not present'}{aliases.length?' · Aliases: '+aliases.join(', '):''}</p>
    {parsed&&<p>Inspection Parsed Value: {parsed.parsedValue===null?'Not supplied / invalid':String(parsed.parsedValue)} · {parsed.valueSource} · before Setup</p>}
    <p>{p.applicability.description}</p><p>Requirement: {parsed?.requirement.state??p.requirement.kind}</p>
    {!!p.options?.availability&&<p>{String(p.options.availability)}</p>}
    {!!p.options?.unavailableReason&&<p>{String(p.options.unavailableReason)} · {String(p.options.unavailableValues)}</p>}
   </details>
   {p.path&&<div className="path-preflight"><small>Host path: {path?path.status+' · '+path.message:'Not checked · matching Host inspection unavailable'}</small>{path&&<details><summary>Path resolution</summary><p>Core cwd: {path.cwd}</p><p>Resolved: {path.resolvedPath??'Not set'}</p>{path.parent&&<p>Parent: {path.parent}</p>}</details>}</div>}
  </div>;
 }
 const axisKeys=new Set(axes?.flatMap(a=>[a.blocksKey,a.minKey,a.maxKey,a.lowerBoundaryKey,a.upperBoundaryKey])??[1,2,3].flatMap(i=>['nblockx'+i,'x'+i+'_min','x'+i+'_max','x'+i+'l_boundary_type','x'+i+'r_boundary_type']));
 function groupBody(group:string){
  const members=rows.filter(r=>r.parameter.group===group);
  const placed=(kind:'common'|'advanced'|'inactive')=>members.filter(r=>parameterPlacement(r,rows,inspection,layout,errors)===kind);
  if(group==='Grid')return <>
   <p>Dimension: {layout?.dimension??'Unavailable'} · {coordinates?'Core coordinate catalog':'Last valid layout; correct invalid input'} · 0 = off, ≥1 = on; x3 requires x2</p>
   {[1,2,3].map((n,i)=>{const axis=axes?.[i],blocks=find(axis?.blocksKey??'nblockx'+n);return <section className="grid-axis-block" key={n} aria-label={'Grid x'+n+' axis'}><h4>x{n} · {axis?.displayName??'Coordinate unavailable'} · {axis?(axis.active?'active':'off'):'unknown'}</h4>{blocks&&render(blocks)}{<div hidden={axis?!axis.active:false}>{(axis?[axis.minKey,axis.maxKey,axis.lowerBoundaryKey,axis.upperBoundaryKey]:['x'+n+'_min','x'+n+'_max','x'+n+'l_boundary_type','x'+n+'r_boundary_type']).map(k=>{const row=find(k);return row?render(row):null;})}</div>}</section>;})}
   {members.filter(r=>!axisKeys.has(r.parameter.key)&&r.parameter.presentation?.subgroup!=='AMR').map(render)}
   <section className="grid-axis-block" aria-label="Adaptive Mesh Refinement (AMR)"><h4>Adaptive Mesh Refinement (AMR)</h4>{members.filter(r=>r.parameter.presentation?.subgroup==='AMR'&&r.parameter.key!=='regrid_interval').map(render)}
    <details><summary>AMR Advanced</summary>{members.filter(r=>r.parameter.presentation?.subgroup==='AMR'&&r.parameter.key==='regrid_interval').map(render)}</details>
    {inspection?.amrIndicators&&<details><summary>Refinement field applicability</summary><p>{inspection.amrIndicators.speciesResolution}</p><ul>{inspection.amrIndicators.choices.map(c=><li key={c.value}>{c.value} · {c.available?'available':'unavailable'}{c.selected?' · selected':''}{c.reason?' · '+c.reason:''}</li>)}</ul></details>}
   </section>
  </>;
  if(group==='Runtime'){
   const sorted=[...members].sort(runtimeOrder);
   return ['Execution','Termination','Output','Checkpoint / Restart','Advanced repair / device / time'].map(name=>{
    const subset=sorted.filter(r=>runtimeSection(r.parameter.key)===name);
    return name.startsWith('Advanced')?<details key={name}><summary>{name}</summary>{subset.map(render)}</details>:<section key={name} aria-label={name+' parameters'}><h4>{name}</h4>{subset.map(render)}</section>;
   });
  }
  const common=placed('common').sort((a,b)=>['gravity_type','use_diffusion','use_burn'].includes(a.parameter.key)?-1:['gravity_type','use_diffusion','use_burn'].includes(b.parameter.key)?1:0);
  return <>
   {group==='Gravity'&&<p>Configuring gravity does not enable gravity field Preview.</p>}
   {group==='Diffusion'&&<p>{inspection?.diffusion?inspection.diffusion.source+' · '+inspection.diffusion.sourceScope:'Awaiting matching Core transport applicability.'} Transport mode is not user-selectable.</p>}
   {common.map(render)}
   {!!placed('advanced').length&&<details><summary>{group==='Network'?'Network ODE / Advanced':group+' Advanced'}</summary>{placed('advanced').map(render)}</details>}
   {!!placed('inactive').length&&<details><summary>Inactive settings · text retained ({placed('inactive').length})</summary><p>Inactive controls remain searchable. Hiding a field never resolves an error.</p>{placed('inactive').map(render)}</details>}
  </>;
 }
 return <section className="standard-catalog" aria-label="Standard parameter catalog">
  <p>{schema.parameters.length} standard keys · {rows.length} controls · aliases share one control</p>
  <input className="parameter-search" aria-label="Search all standard parameters" placeholder="Search name, raw key or description…" value={search} onChange={e=>setSearch(e.target.value)}/>
  <nav className="block-navigator" aria-label="Standard parameter groups">{groups.map(g=><button key={g} aria-pressed={!!expanded[g]} onClick={()=>{setSearch('');setExpanded(v=>({...v,[g]:!v[g]}));}}>{g}</button>)}</nav>
  {query?<><h3>Search results</h3>{rows.filter(matches).map(render)}{!rows.some(matches)&&<p>No matching standard parameters.</p>}</>:groups.map(g=>{
   const members=rows.filter(r=>r.parameter.group===g),issues=members.filter(r=>errors[r.sourceKey]||errors[r.parameter.key]||inspection?.diagnostics.some(d=>d.parameterKey===r.parameter.key&&d.severity==='error')).length;
   const first=members.find(r=>['gravity_type','use_diffusion','use_burn','eos_type','geometry','compute_backend'].includes(r.parameter.key));
   return <details className="standard-group" key={g} open={!!expanded[g]} onToggle={e=>{const open=e.currentTarget.open;setExpanded(v=>v[g]===open?v:{...v,[g]:open});}}><summary>{g}<small>{first?first.parameter.key+' = '+first.value:members.length+' parameters'} · {issues} issues</small></summary>{groupBody(g)}</details>;
  })}
 </section>;
}
