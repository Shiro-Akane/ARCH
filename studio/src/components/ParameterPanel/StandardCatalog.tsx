import {useState} from 'react';
import type {ReactNode} from 'react';
import type {ConfigurationSchema,ConfigurationInspection,CoordinateSystem,PathCheck} from '../../host/configurationContracts';
import {catalog,catalogCoordinates,catalogOptions,parameterGroups,parameterUnit} from '../../data/parameterCatalog';
import {forbiddenParameters,parameterPlacement,runtimeOrder,runtimeSection} from '../../data/parameterLayout';
import {authoritativeAxisLabel,compactParameterUnit,orderedParameterGroups,parameterGroupLabel} from './parameterPresentation';
import {ConfigControl} from './ConfigControl';
/** Layout never inserts defaults, deletes hidden text, or interprets physical formulas. */
export function StandardCatalog({schema,values,inspection,pathChecks,errors,onEdit,onSelect,onRemove,caseParameters,caseParameterCount=0}:{schema:ConfigurationSchema;values:Record<string,string>;inspection:ConfigurationInspection|undefined;pathChecks:PathCheck[]|undefined;errors:Record<string,string>;onEdit:(key:string,value:string)=>void;onSelect:(key:string)=>void;onRemove?:(key:string)=>void;caseParameters?:(query:string)=>ReactNode;caseParameterCount?:number}){
 const [expanded,setExpanded]=useState<Record<string,boolean>>({Runtime:true});
 const [search,setSearch]=useState('');
 const rows=catalog(schema.parameters,values),coordinates=catalogCoordinates(schema,values);
 const [lastCoordinates,setLastCoordinates]=useState<CoordinateSystem|undefined>(coordinates);
 if(coordinates&&coordinates!==lastCoordinates)setLastCoordinates(coordinates);
 const layout=coordinates??lastCoordinates,axes=layout?.axes;
 const query=search.trim().toLowerCase(),forbidden=forbiddenParameters(inspection);
 const groups=orderedParameterGroups([...parameterGroups,...rows.map(row=>row.parameter.group)],caseParameterCount>0);
 const matches=(row:typeof rows[number])=>[row.parameter.key,...row.aliases,row.parameter.presentation?.displayName,row.parameter.presentation?.description,row.parameter.group].join(' ').toLowerCase().includes(query);
 const find=(key:string)=>rows.find(row=>row.parameter.key===key);
 const extraCase=caseParameters?.(query);
 function render(row:typeof rows[number]){
  const {parameter:p,sourceKey,value,explicit,aliases}=row;
  const parsed=inspection?.parameters.find(item=>item.key===p.key),path=pathChecks?.find(item=>item.key===p.key);
  const diagnostics=inspection?.diagnostics.filter(item=>item.parameterKey===p.key)??[];
  const unit=compactParameterUnit(p,parsed,inspection?.coordinates),toggle=p.presentation?.toggle;
  const unavailable=forbidden.has(p.key);
  const coefficientBlocked=false; // Unknown applicability must not prevent filling required input.
  const labels=Object.fromEntries(Array.isArray(p.options?.choices)?p.options.choices.flatMap(choice=>choice&&typeof choice==='object'&&typeof choice.value==='string'&&typeof choice.displayName==='string'?[[choice.value,choice.displayName]]:[]):[]);
  const choices=p.options?.choices;
  const choice=Array.isArray(choices)?choices.find(choice=>choice&&typeof choice==='object'&&Array.isArray(choice.acceptedNames)&&choice.acceptedNames.some((name:unknown)=>typeof name==='string'&&(p.options?.caseSensitive===false?name.toLowerCase()===value.toLowerCase():name===value))):undefined;
  const shown=choice&&typeof choice.value==='string'?choice.value:value;
  return <div className="parameter-field" key={p.key} data-standard-key={p.key} onFocus={()=>onSelect(sourceKey)}>
   <div className="parameter-label-row"><span className="parameter-label" title={p.presentation?.description}>{p.presentation?.displayName??p.key.replaceAll('_',' ')}</span>{unit&&<small className="parameter-unit">{unit}</small>}</div>
   {toggle&&<label className="parameter-toggle"><input type="checkbox" aria-label={p.key+' enabled'} checked={Number(value)>0} onChange={event=>onEdit(sourceKey,event.target.checked?'':String(toggle.offValue))}/>Enabled</label>}
   <fieldset className="parameter-control" disabled={unavailable||coefficientBlocked}>
    <ConfigControl optionLabels={labels} name={p.key} value={shown} authoritative meta={{group:'Runtime',type:p.type==='string'?'text':p.type,evidence:'ARCH --config-schema',options:catalogOptions(p)}} error={errors[sourceKey]??errors[p.key]} onChange={next=>onEdit(sourceKey,next)}/>
   </fieldset>
   {toggle&&value===''&&<small>Enter a positive value to enable; no value is invented.</small>}
   {parsed?.applicability.state==='unknown-dependency'&&<small>Applicability unresolved: {parsed.applicability.missingDependencies.join(', ')}</small>}
   {parsed?.applicability.state==='not-applicable'&&explicit&&<small>Not applicable in current inspection. Existing text retained.</small>}
   {(errors[sourceKey]??errors[p.key])&&<small className="validation-error">{errors[sourceKey]??errors[p.key]}</small>}
   {diagnostics.map((diagnostic,index)=><small key={index} className={diagnostic.severity==='error'?'validation-error':''}>{diagnostic.code}: {diagnostic.message}</small>)}
   {unavailable&&explicit&&onRemove&&<button type="button" onClick={()=>onRemove(sourceKey)}>Remove forbidden parameter {sourceKey}</button>}
   {coefficientBlocked&&!unavailable&&<small>Constant coefficient editing requires matching Core permission; existing text retained.</small>}
   {unavailable&&<small>Core forbids this explicit parameter. Remove explicitly; Undo and Save remain separate.</small>}
   {p.path&&(!path||path.status!=='ok')&&<div className="path-preflight"><small className={path?.status==='error'?'validation-error':undefined}>Host path: {path?path.status+' · '+path.message:'Not checked · matching Host inspection unavailable'}</small></div>}
   <details className="parameter-help"><summary aria-label={'Parameter help for '+p.key}>Help</summary>
    <p className="parameter-key">Raw key: {p.key}</p>{p.presentation?.description&&<p>{p.presentation.description}</p>}
    <p>Type: {p.type} · Unit: {parameterUnit(p,parsed,inspection?.coordinates)}</p>
    <p>Schema Default: {p.allowedDefault?String(p.allowedDefault.value):'None permitted'} · Source: {p.allowedDefault?.source??'No default'}</p>
    <p>{explicit?'Explicit Working Copy':'Missing from Working Copy · not written'} · Working token: {explicit?value:'not present'}{aliases.length?' · Aliases: '+aliases.join(', '):''}</p>
    {parsed&&<><p>Inspection Parsed Value: {parsed.parsedValue===null?'Not supplied / invalid':String(parsed.parsedValue)} · {parsed.valueSource} · before Setup</p><p>Resolved Value: {parsed.resolvedValue===null?'Unavailable':String(parsed.resolvedValue)} · before Setup</p></>}
    <p>{p.applicability.description}</p><p>Requirement: {parsed?.requirement.state??p.requirement.kind}</p>
    {!!p.options?.availability&&<p>{String(p.options.availability)}</p>}
    {!!p.options?.unavailableReason&&<p>{String(p.options.unavailableReason)} · {String(p.options.unavailableValues)}</p>}
    {p.path&&<div className="path-preflight"><p>Host path: {path?path.status+' · '+path.message:'Not checked · matching Host inspection unavailable'}</p>{path&&<><p>Core cwd: {path.cwd}</p><p>Resolved: {path.resolvedPath??'Not set'}</p>{path.parent&&<p>Parent: {path.parent}</p>}</>}</div>}
   </details>
  </div>;
 }
 const axisKeys=new Set(axes?.flatMap(axis=>[axis.blocksKey,axis.minKey,axis.maxKey,axis.lowerBoundaryKey,axis.upperBoundaryKey])??[1,2,3].flatMap(index=>['nblockx'+index,'x'+index+'_min','x'+index+'_max','x'+index+'l_boundary_type','x'+index+'r_boundary_type']));
 function groupBody(group:string){
  const members=rows.filter(row=>row.parameter.group===group);
  const placed=(kind:'common'|'advanced'|'inactive')=>members.filter(row=>parameterPlacement(row,rows,inspection,layout,errors)===kind);
  if(group==='Grid')return <>
   {find('geometry')&&render(find('geometry')!)}
   <p className="coordinate-status">Dimension: {layout?.dimension??'Unavailable'}{!coordinates&&' · Last valid layout; correct invalid input'}</p>
   {[1,2,3].map((number,index)=>{const axis=axes?.[index],blocks=find(axis?.blocksKey??'nblockx'+number);return <section className="grid-axis-block" key={number} aria-label={'Grid x'+number+' axis'}>
    <h4>{authoritativeAxisLabel(axis)}<span className="axis-state">{axis?(axis.active?'active':'off'):'unknown'}</span></h4>
    {blocks&&render(blocks)}<div hidden={axis?!axis.active:false}>{(axis?[axis.minKey,axis.maxKey,axis.lowerBoundaryKey,axis.upperBoundaryKey]:['x'+number+'_min','x'+number+'_max','x'+number+'l_boundary_type','x'+number+'r_boundary_type']).map(key=>{const row=find(key);return row?render(row):null;})}</div>
    <details className="axis-help"><summary>Axis details</summary><p>Core axis: {axis?.key??'x'+number} · native coordinate: {axis?.nativeName??'Unavailable'}</p><p>Blocks: 0 = off, ≥1 = on; x3 requires x2.</p></details>
   </section>;})}
   {members.filter(row=>!axisKeys.has(row.parameter.key)&&row.parameter.key!=='geometry'&&row.parameter.presentation?.subgroup!=='AMR').map(render)}
   <section className="grid-axis-block" aria-label="Adaptive Mesh Refinement (AMR)"><h4>Adaptive Mesh Refinement (AMR)</h4>{members.filter(row=>row.parameter.presentation?.subgroup==='AMR'&&row.parameter.key!=='regrid_interval').map(render)}
    <details><summary>AMR Advanced</summary>{members.filter(row=>row.parameter.presentation?.subgroup==='AMR'&&row.parameter.key==='regrid_interval').map(render)}</details>
    {inspection?.amrIndicators&&<details><summary>Refinement field applicability</summary><p>{inspection.amrIndicators.speciesResolution}</p><ul>{inspection.amrIndicators.choices.map(choice=><li key={choice.value}>{choice.value} · {choice.available?'available':'unavailable'}{choice.selected?' · selected':''}{choice.reason?' · '+choice.reason:''}</li>)}</ul></details>}
   </section>
  </>;
  if(group==='Runtime'){
   const sorted=[...members].sort(runtimeOrder);
   return ['Execution','Termination','Output','Checkpoint / Restart','Advanced repair / device / time'].map(name=>{
    const subset=sorted.filter(row=>runtimeSection(row.parameter.key)===name);
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
   {group==='Case'&&extraCase}
  </>;
 }
 return <section className="standard-catalog" aria-label="Parameter catalog">
  <input className="parameter-search" aria-label="Search all parameters" placeholder="Search name, raw key or description…" value={search} onChange={event=>setSearch(event.target.value)}/>
  <nav className="block-navigator" aria-label="Parameter groups">{groups.map(group=><button key={group} aria-pressed={!!expanded[group]} onClick={()=>{setSearch('');setExpanded(previous=>({...previous,[group]:!previous[group]}));}}>{parameterGroupLabel(group)}</button>)}</nav>
  {query?<><h3>Search results</h3>{rows.filter(matches).map(render)}{extraCase}{!rows.some(matches)&&!extraCase&&<p>No matching parameters.</p>}</>:groups.map(group=>{
   const members=rows.filter(row=>row.parameter.group===group),issues=members.filter(row=>errors[row.sourceKey]||errors[row.parameter.key]||inspection?.diagnostics.some(diagnostic=>diagnostic.parameterKey===row.parameter.key&&diagnostic.severity==='error')).length;
   return <details className="standard-group" key={group} open={!!expanded[group]} onToggle={event=>{const open=event.currentTarget.open;setExpanded(previous=>previous[group]===open?previous:{...previous,[group]:open});}}><summary>{parameterGroupLabel(group)}<small>{members.length+(group==='Case'?caseParameterCount:0)} parameters{issues?' · '+issues+' issues':''}</small></summary>{groupBody(group)}</details>;
  })}
  <details className="catalog-help"><summary>Catalog details</summary><p>{schema.parameters.length} catalog keys · {rows.length} controls · aliases share one control</p></details>
 </section>;
}
