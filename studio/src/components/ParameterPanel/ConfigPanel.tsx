import {useCoreParameters} from '../../state/coreParameters';
import type {WorkingCopy} from '../../data/RealInitPreviewProvider';
import {serializePar} from '../../data/ParDocument';
import {useHost} from '../../host/hostContext';
import {ConfigAdapter,ConfigRequestError} from '../../host/ConfigAdapter';
import {associateConfig,diskStatus} from '../../host/configLifecycle';
import type {ConfigLifecycleState,ConfigReadResponse} from '../../host/contracts';
import { EditHistory, historyShortcut } from '../../state/editHistory';
import type { ParameterDetails } from '../Inspector/ParameterInspector';
import { useEffect, useRef, useState } from 'react';
import { createLatestRequest, selectedFile } from '../../data/latestRequest';
import { enumControls } from './controlContract';
import { ConfigControl } from './ConfigControl';
import { PairedFields } from './PairedFields';
import { BlockNavigator } from './BlockNavigator';
import { blockSummary, observedDimension, advancedKey, matchesParameter, friendlyLabels } from './panelPresentation';
import type { CoreGroup } from '../../data/parSchema';
import { parSchema } from '../../data/parSchema';
import { effectiveEntries } from '../../data/ParDocument';
import { loadPar, editPar, parStatus, parErrors, revertPar, exportPar } from '../../state/parState';
import type { ParState } from '../../state/parState';
export function ConfigPanel({ onEdit, onInspect, active, onWorkingCopy }: { active:boolean; onWorkingCopy?:(copy:WorkingCopy|null)=>void; onEdit: () => void; onInspect:(p:ParameterDetails|null)=>void }) {
  const core=useCoreParameters();const registerEdit=core.registerEdit;const registerFocus=core.registerFocus;
  const host=useHost();const api=useRef(new ConfigAdapter());
  const [lifecycle,setLifecycle]=useState<ConfigLifecycleState>({diskState:'unknown'});
  const [pending,setPending]=useState<'project'|'reload'|'browser'|null>(null);
  const [busy,setBusy]=useState(false);const [conflict,setConflict]=useState(false);const [saveAsOpen,setSaveAsOpen]=useState(false);const [destination,setDestination]=useState('');
  const history=useRef(new EditHistory<ParState>());
  const [selectedKey,setSelectedKey]=useState<string|null>(null);
  const [state,setState] = useState<ParState|null>(null);
  const [message,setMessage] = useState('');
  const [query,setQuery]=useState('');
  const [customQuery,setCustomQuery]=useState('');
  const [block,setBlock]=useState<CoreGroup>('Grid');
  const latest=useRef(createLatestRequest());
  const fileInput=useRef<HTMLInputElement>(null);
  useEffect(()=>{if(!message)return;const timer=setTimeout(()=>setMessage(''),6000);return ()=>clearTimeout(timer);},[message]);
  useEffect(()=>{if(!state){onWorkingCopy?.(null);return;}try{onWorkingCopy?.({text:serializePar(state.document,state.changes),filename:state.filename,valid:parStatus(state)!=='invalid',dirty:parStatus(state)!=='saved'});}catch{onWorkingCopy?.({text:state.document.raw,filename:state.filename,valid:false,dirty:true});}},[state,onWorkingCopy]);
  const errors=state ? parErrors(state) : {};
  const values=state ? Object.fromEntries(effectiveEntries(state.document).map(e=>[e.key,state.changes[e.key] ?? e.value])) : {};
  const dimension=observedDimension(values);
  useEffect(()=>{if(!active)return;const handler=(e:KeyboardEvent)=>{if(e.defaultPrevented||!state||busy)return;const action=historyShortcut(e.key,e.ctrlKey,e.metaKey,e.shiftKey);if(action){e.preventDefault();setState(history.current[action](state));onEdit();}};window.addEventListener('keydown',handler);return()=>window.removeEventListener('keydown',handler);},[active,state,onEdit,busy]);
  useEffect(()=>{
    const entry=state && (effectiveEntries(state.document).find(e=>e.key===selectedKey)??(selectedKey&&core.snapshot?.result.core.parameterMetadata?.parameters.some(m=>m.key===selectedKey)?{key:selectedKey,value:'',line:0}:undefined));
    const meta=entry ? parSchema[entry.key] : undefined;
    onInspect(entry && state ? {key:entry.key,label:friendlyLabels[entry.key] ?? entry.key,value:state.changes[entry.key] ?? (entry.line===0?String(core.snapshot?.result.core.parameterMetadata?.parameters.find(m=>m.key===entry.key)?.defaultValue??''):entry.value),raw:entry.value,saved:lifecycle.association?entry.value:undefined,workingSource:Object.hasOwn(state.changes,entry.key)||entry.line>0?'explicit':'default',line:entry.line,type:meta?.type ?? 'untyped text',options:enumControls[entry.key] ?? meta?.options,range:meta?.range,error:parErrors(state)[entry.key],evidence:enumControls[entry.key] ? 'src/driver/dispatch/PolicyDescriptor.h' : meta?.evidence} : null);
  },[state,selectedKey,onInspect,core.snapshot,lifecycle.association]);
  useEffect(()=>{registerEdit((key,value)=>{if(!state||busy)return;const next=editPar(state,key,value);history.current.record(state,next);setState(next);setSelectedKey(key);onEdit();});return()=>{registerEdit(null);};},[registerEdit,state,busy,onEdit]);
  const coreParameters=core.snapshot?.result.identity.projectId===host.snapshot?.session.projectId?core.snapshot?.result.core.parameterMetadata?.parameters??[]:[];
  const inserted=state?Object.keys(state.changes).filter(key=>!effectiveEntries(state.document).some(e=>e.key===key)):[];
  const missing=state?coreParameters.filter(m=>!effectiveEntries(state.document).some(e=>e.key===m.key)):[];
  useEffect(()=>{registerFocus(setSelectedKey);return()=>registerFocus(null);},[registerFocus]);
  const associated=!!lifecycle.association;
  const writable=host.connected&&host.snapshot?.host.capabilities.writeConfig===true;
  const canSave=writable&&associated&&host.snapshot?.session.projectId===lifecycle.association?.projectId&&host.snapshot?.session.parameterFile?.relativePath===lifecycle.association?.relativePath;
  const disk=['changed-externally','missing','read-error'].includes(lifecycle.diskState)?lifecycle.diskState:host.snapshot?diskStatus(lifecycle,host.snapshot):'unknown';
  function loaded(read:ConfigReadResponse,replace:boolean){const next=associateConfig(read);history.current.reset();setSelectedKey(null);setState(next.state);setLifecycle(next.lifecycle);setConflict(false);if(replace)onEdit();}
  function failed(error:unknown){setMessage(error instanceof Error?error.message:'Configuration operation failed; Working Copy kept.');if(error instanceof ConfigRequestError){if(error.info.code==='changed-externally'){setConflict(true);setLifecycle(v=>({...v,diskState:'changed-externally'}));}if(error.info.code==='not-found')setLifecycle(v=>({...v,diskState:'missing'}));}}
  async function openProject(){if(busy||!host.connected)return;setBusy(true);try{loaded(await api.current.read(),true);setMessage('Project configuration loaded');}catch(error){failed(error);}finally{setBusy(false);}}
  async function save(){if(!state||!canSave||busy||parStatus(state)==='invalid')return false;setBusy(true);try{const a=lifecycle.association!;const result=await api.current.save({projectId:a.projectId,relativePath:a.relativePath,expectedFingerprint:lifecycle.savedFingerprint!,text:exportPar(state).text});loaded(result,false);host.update(result.project);setMessage('Configuration saved to disk');return true;}catch(error){failed(error);return false;}finally{setBusy(false);}}
  function executeReplace(action:'project'|'reload'|'browser'){setPending(null);if(action==='browser')fileInput.current?.click();else void openProject();}
  function requestReplace(action:'project'|'reload'|'browser'){if(state&&parStatus(state)!=='saved')setPending(action);else executeReplace(action);}
  function downloadCopy(){if(!state||parStatus(state)==='invalid')return;const result=exportPar(state);const url=URL.createObjectURL(new Blob([result.text],{type:'text/plain;charset=utf-8'}));const link=document.createElement('a');link.href=url;link.download=result.filename;link.click();setTimeout(()=>URL.revokeObjectURL(url),60000);setMessage('Download copy requested');}
  function beginSaveAs(){if(!state)return;setDestination(state.filename.replace(/\.par$/i,'')+'_copy.par');setSaveAsOpen(true);}
  async function saveAs(){if(!state||!writable||!host.snapshot||busy||parStatus(state)==='invalid')return;setBusy(true);try{const path=destination.endsWith('.par')?destination:destination+'.par';const result=await api.current.saveAs({projectId:host.snapshot.session.projectId,destinationRelativePath:path,text:exportPar(state).text});loaded(result,false);host.update(result.project);setSaveAsOpen(false);setMessage('Saved as new current project configuration');if(pending)executeReplace(pending);}catch(error){failed(error);}finally{setBusy(false);}}
  function renderEntry(entry: ReturnType<typeof effectiveEntries>[number]) {
    if (!state) return null;
    return <label className="parameter-field" key={entry.key} onFocus={()=>setSelectedKey(entry.key)} onClick={()=>setSelectedKey(entry.key)}><span title={`Raw key: ${entry.key}; Source line: ${entry.line}; Type: ${parSchema[entry.key]?.type ?? 'untyped text'}; Contract: ${enumControls[entry.key] ? 'src/driver/dispatch/PolicyDescriptor.h; Allowed values: '+enumControls[entry.key].join(', ') : parSchema[entry.key]?.evidence ?? 'No metadata'}${parSchema[entry.key]?.options ? '; Allowed values: '+parSchema[entry.key].options?.join(', ') : ''}${parSchema[entry.key]?.range ? '; Range: '+parSchema[entry.key].range?.join(' to ') : ''}${state.document.entries.filter(e=>e.key===entry.key).length>1 ? '; last occurrence is effective' : ''}`}>{friendlyLabels[entry.key] ?? (entry.key || '(empty key)')}</span><ConfigControl name={entry.key || 'Empty key'} value={state.changes[entry.key] ?? entry.value} meta={parSchema[entry.key]} error={errors[entry.key]} onChange={value => {const next=editPar(state,entry.key,value);history.current.record(state,next);setState(next);onEdit();}} />{errors[entry.key] && <small className="validation-error">{errors[entry.key]}</small>}</label>;
  }
  return <aside className="parameter-panel panel" aria-label="Real configuration" onKeyDown={e=>{const action=historyShortcut(e.key,e.ctrlKey,e.metaKey,e.shiftKey);if(action&&state&&!busy){e.preventDefault();e.stopPropagation();setState(history.current[action](state));onEdit();}}} onPointerDownCapture={e=>{if(state&&e.target instanceof HTMLInputElement&&(e.target.type==='range'||e.target.dataset.numeric==='true'))history.current.begin(state);}} onPointerUp={()=>{if(state)history.current.end();}} onPointerCancel={()=>{if(state)history.current.end();}}>
    <div className="panel-heading"><h2>Parameters</h2><span title="Local working copy. Browser mode exports a new file; original is not overwritten.">REAL CONFIG</span></div>
    <div className="parameter-scroll"><fieldset disabled={busy} className="config-lifecycle-fieldset">
      <section className="config-file"><h3>Config file</h3><p className="config-filename" title={state?.filename}>{state?.filename ?? 'No configuration loaded'}</p><button type="button" onClick={()=>requestReplace('browser')}>Open Config…</button><button disabled={!host.connected} onClick={()=>requestReplace('project')}>Open Project Config</button><input ref={fileInput} hidden type="file" accept=".par" aria-label="Config file picker" onChange={async e => {
        const file=selectedFile(e.target.files); e.target.value=''; if(!file)return;
        setMessage('Opening…');setBusy(true);
        await latest.current(async () => {
          if(file.size>1024*1024)throw new Error('Config size limit: 1 MiB.');
          const raw=new TextDecoder('utf-8',{fatal:true,ignoreBOM:true}).decode(await file.arrayBuffer());
          return loadPar(file.name,raw);
        }, next => {history.current.reset();setSelectedKey(null);setState(next);setLifecycle({diskState:'unknown'});setConflict(false);onEdit();setMessage('Config loaded');}, error => setMessage(error instanceof Error ? error.message : 'Could not read config.'));setBusy(false);
      }} /></section>
      <div className="config-notification" role="status">{message}</div>
      {associated&&<p aria-label="Disk status">Disk: {disk}</p>}
      {associated&&<p className="section-note">Local Host: {lifecycle.association?.relativePath}</p>}
      {associated&&<button disabled={!host.connected} onClick={()=>requestReplace('reload')}>Reload disk version</button>}
      {conflict&&<div role="alert" className="config-conflict"><p>This configuration file changed on disk. Your unsaved Working Copy has been kept.</p><button onClick={()=>requestReplace('reload')}>Reload disk version</button><button onClick={beginSaveAs}>Save Working Copy As…</button><button onClick={()=>setConflict(false)}>Cancel</button></div>}
      {pending&&<div role="dialog" aria-label="Unsaved changes" className="config-conflict"><p>You have unsaved changes. Your Working Copy will be replaced only after your choice.</p><button disabled={!canSave||!state||parStatus(state)==='invalid'} onClick={async()=>{if(await save())executeReplace(pending);}}>Save and continue</button>{writable?<button onClick={beginSaveAs}>Save As…</button>:<button disabled={!state||parStatus(state)==='invalid'} onClick={downloadCopy}>Download Copy…</button>}<button onClick={()=>executeReplace(pending)}>{pending==='reload'?'Discard and Reload':'Discard and Open'}</button><button onClick={()=>setPending(null)}>Cancel replacement</button></div>}
      {saveAsOpen&&<div role="dialog" aria-label="Save Working Copy As" className="config-conflict"><label>Project-relative destination<input aria-label="Project-relative destination" value={destination} onChange={e=>setDestination(e.target.value)}/></label><button disabled={!writable||!state||parStatus(state)==='invalid'} onClick={()=>void saveAs()}>Save new file</button><button onClick={()=>setSaveAsOpen(false)}>Cancel Save As</button></div>}
      {state && <><p className={`config-state ${parStatus(state)}`} aria-label="Config status">{parStatus(state)}{Object.keys(errors).length>0 ? ` · ${Object.keys(errors).length} issues` : ''}</p>{Object.keys(errors).length>0 && <details className="config-issues"><summary>View issues</summary><ul>{Object.entries(errors).map(([key,error])=><li key={key}>{key}: {error}</li>)}</ul></details>}
      <BlockNavigator selected={block} onSelect={next=>{setBlock(next);setQuery('');}} summary={group=>blockSummary(group,Object.fromEntries(effectiveEntries(state.document).map(e=>[e.key,state.changes[e.key] ?? e.value])))} />
      {([block]).map(group => <section className="parameter-group core-group" key={group}><h3>{group}</h3><div className="group-content"><input className="parameter-search" aria-label="Search current block" placeholder="Search this block…" value={query} onChange={e=>setQuery(e.target.value)} />{group==='Grid' && <p>Dimension: {dimension ? `${dimension}D` : 'not determined from explicit values'}</p>}
      <PairedFields entries={effectiveEntries(state.document).filter(entry => parSchema[entry.key]?.group === group && matchesParameter(entry.key,values[entry.key],query) && (query.trim() || !advancedKey(entry.key,group,dimension)))} renderEntry={renderEntry} />
      {!query.trim() && effectiveEntries(state.document).some(entry=>parSchema[entry.key]?.group===group && advancedKey(entry.key,group,dimension)) && <details className="advanced-parameters"><summary>Advanced ({effectiveEntries(state.document).filter(entry=>parSchema[entry.key]?.group===group && advancedKey(entry.key,group,dimension)).length})</summary><PairedFields entries={effectiveEntries(state.document).filter(entry=>parSchema[entry.key]?.group===group && advancedKey(entry.key,group,dimension))} renderEntry={renderEntry} /></details>}
      </div></section>)}
      {missing.length>0&&<section className="parameter-group"><h3>Core parameters · defaults / inserted</h3>{missing.map(m=><label className="parameter-field" key={m.key} onFocus={()=>setSelectedKey(m.key)}><span>{m.key} · {inserted.includes(m.key)?'explicit Working Copy':'default · from previous Core response'}</span><input aria-label={m.key} value={state.changes[m.key]??String(m.defaultValue??'')} onChange={e=>core.edit(m.key,e.target.value)}/><small>Default {String(m.defaultValue??'unavailable')} · {m.unit??'Unit not provided'}</small></label>)}</section>}
      <section className="custom-search"><h3>Custom Parameters</h3><input className="parameter-search" aria-label="Search custom parameters" placeholder="Search custom parameters…" value={customQuery} onChange={e=>setCustomQuery(e.target.value)} />
      <details className="parameter-group custom-group" open={!!customQuery.trim()}><summary>All Custom Parameters ({effectiveEntries(state.document).filter(entry=>!parSchema[entry.key] && matchesParameter(entry.key,values[entry.key],customQuery)).length})</summary><div className="group-content">{effectiveEntries(state.document).filter(entry=>!parSchema[entry.key] && matchesParameter(entry.key,values[entry.key],customQuery)).map(renderEntry)}</div></details></section>
      <div className="config-actions"><button disabled={!canSave||parStatus(state)==='invalid'} onClick={()=>void save()}>Save</button><button disabled={!writable||parStatus(state)==='invalid'} onClick={beginSaveAs}>Save Working Copy As…</button><button disabled={parStatus(state)==='saved'} onClick={()=>{history.current.reset();setState(revertPar(state));setMessage('Config reverted');onEdit();}}>Revert</button>
      <button disabled={parStatus(state)==='invalid'} onClick={()=>{
        const result=exportPar(state); const url=URL.createObjectURL(new Blob([result.text],{type:'text/plain;charset=utf-8'}));
        const link=document.createElement('a');link.href=url;link.download=result.filename;link.click();
        setTimeout(()=>URL.revokeObjectURL(url),60000);setMessage('Save As requested');
      }} title={parStatus(state)==='invalid' ? 'Resolve validation issues before exporting.' : 'Exports a new file; original is not overwritten.'}>Download Copy…</button></div>
      <details className="raw-source"><summary>Raw .par · loaded source (read-only)</summary><p>{state.document.rawLines.length} raw lines preserved unchanged.</p><p>Original loaded file; edits remain in the GUI working copy. Source line numbers are shown below.</p><pre tabIndex={0}>{state.document.raw.split(/\r\n|\n|\r/).map((line,i)=>`${i+1}  ${line}`).join('\n')}</pre></details>
      </>}
    </fieldset></div>
  </aside>;
}
