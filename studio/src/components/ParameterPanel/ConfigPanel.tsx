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
export function ConfigPanel({ onEdit }: { onEdit: () => void }) {
  const [state,setState] = useState<ParState|null>(null);
  const [message,setMessage] = useState('');
  const [query,setQuery]=useState('');
  const [customQuery,setCustomQuery]=useState('');
  const [block,setBlock]=useState<CoreGroup>('Grid');
  const latest=useRef(createLatestRequest());
  const fileInput=useRef<HTMLInputElement>(null);
  useEffect(()=>{if(!message)return;const timer=setTimeout(()=>setMessage(''),6000);return ()=>clearTimeout(timer);},[message]);
  const errors=state ? parErrors(state) : {};
  const values=state ? Object.fromEntries(effectiveEntries(state.document).map(e=>[e.key,state.changes[e.key] ?? e.value])) : {};
  const dimension=observedDimension(values);
  function renderEntry(entry: ReturnType<typeof effectiveEntries>[number]) {
    if (!state) return null;
    return <label className="parameter-field" key={entry.key}><span title={`Raw key: ${entry.key}; Source line: ${entry.line}; Type: ${parSchema[entry.key]?.type ?? 'untyped text'}; Contract: ${enumControls[entry.key] ? 'src/driver/dispatch/PolicyDescriptor.h; Allowed values: '+enumControls[entry.key].join(', ') : parSchema[entry.key]?.evidence ?? 'No metadata'}${parSchema[entry.key]?.options ? '; Allowed values: '+parSchema[entry.key].options?.join(', ') : ''}${parSchema[entry.key]?.range ? '; Range: '+parSchema[entry.key].range?.join(' to ') : ''}${state.document.entries.filter(e=>e.key===entry.key).length>1 ? '; last occurrence is effective' : ''}`}>{friendlyLabels[entry.key] ?? (entry.key || '(empty key)')}</span><ConfigControl name={entry.key || 'Empty key'} value={state.changes[entry.key] ?? entry.value} meta={parSchema[entry.key]} error={errors[entry.key]} onChange={value => {setState(editPar(state,entry.key,value));onEdit();}} />{errors[entry.key] && <small className="validation-error">{errors[entry.key]}</small>}</label>;
  }
  return <aside className="parameter-panel panel" aria-label="Real configuration">
    <div className="panel-heading"><h2>Parameters</h2><span title="Local working copy. Browser mode exports a new file; original is not overwritten.">REAL CONFIG</span></div>
    <div className="parameter-scroll">
      <section className="config-file"><h3>Config file</h3><p className="config-filename" title={state?.filename}>{state?.filename ?? 'No configuration loaded'}</p><button type="button" onClick={()=>fileInput.current?.click()}>Open Config…</button><input ref={fileInput} hidden type="file" accept=".par" aria-label="Config file picker" onChange={async e => {
        const file=selectedFile(e.target.files); e.target.value=''; if(!file)return;
        setState(null);setMessage('Opening…');
        await latest.current(async () => {
          if(file.size>1024*1024)throw new Error('Config size limit: 1 MiB.');
          const raw=new TextDecoder('utf-8',{fatal:true,ignoreBOM:true}).decode(await file.arrayBuffer());
          return loadPar(file.name,raw);
        }, next => {setState(next);setMessage('Config loaded');}, error => setMessage(error instanceof Error ? error.message : 'Could not read config.'));
      }} /></section>
      <div className="config-notification" role="status">{message}</div>
      {state && <><p className={`config-state ${parStatus(state)}`} aria-label="Config status">{parStatus(state)}{Object.keys(errors).length>0 ? ` · ${Object.keys(errors).length} issues` : ''}</p>{Object.keys(errors).length>0 && <details className="config-issues"><summary>View issues</summary><ul>{Object.entries(errors).map(([key,error])=><li key={key}>{key}: {error}</li>)}</ul></details>}
      <BlockNavigator selected={block} onSelect={next=>{setBlock(next);setQuery('');}} summary={group=>blockSummary(group,Object.fromEntries(effectiveEntries(state.document).map(e=>[e.key,state.changes[e.key] ?? e.value])))} />
      {([block]).map(group => <section className="parameter-group core-group" key={group}><h3>{group}</h3><div className="group-content"><input className="parameter-search" aria-label="Search current block" placeholder="Search this block…" value={query} onChange={e=>setQuery(e.target.value)} />{group==='Grid' && <p>Dimension: {dimension ? `${dimension}D` : 'not determined from explicit values'}</p>}
      <PairedFields entries={effectiveEntries(state.document).filter(entry => parSchema[entry.key]?.group === group && matchesParameter(entry.key,values[entry.key],query) && (query.trim() || !advancedKey(entry.key,group,dimension)))} renderEntry={renderEntry} />
      {!query.trim() && effectiveEntries(state.document).some(entry=>parSchema[entry.key]?.group===group && advancedKey(entry.key,group,dimension)) && <details className="advanced-parameters"><summary>Advanced ({effectiveEntries(state.document).filter(entry=>parSchema[entry.key]?.group===group && advancedKey(entry.key,group,dimension)).length})</summary><PairedFields entries={effectiveEntries(state.document).filter(entry=>parSchema[entry.key]?.group===group && advancedKey(entry.key,group,dimension))} renderEntry={renderEntry} /></details>}
      </div></section>)}
      <section className="custom-search"><h3>Custom Parameters</h3><input className="parameter-search" aria-label="Search custom parameters" placeholder="Search custom parameters…" value={customQuery} onChange={e=>setCustomQuery(e.target.value)} />
      <details className="parameter-group custom-group" open={!!customQuery.trim()}><summary>All Custom Parameters ({effectiveEntries(state.document).filter(entry=>!parSchema[entry.key] && matchesParameter(entry.key,values[entry.key],customQuery)).length})</summary><div className="group-content">{effectiveEntries(state.document).filter(entry=>!parSchema[entry.key] && matchesParameter(entry.key,values[entry.key],customQuery)).map(renderEntry)}</div></details></section>
      <div className="config-actions"><button disabled={parStatus(state)==='saved'} onClick={()=>{setState(revertPar(state));setMessage('Config reverted');onEdit();}}>Revert</button>
      <button disabled={parStatus(state)==='invalid'} onClick={()=>{
        const result=exportPar(state); const url=URL.createObjectURL(new Blob([result.text],{type:'text/plain;charset=utf-8'}));
        const link=document.createElement('a');link.href=url;link.download=result.filename;link.click();
        setTimeout(()=>URL.revokeObjectURL(url),60000);setMessage('Save As requested');
      }} title={parStatus(state)==='invalid' ? 'Resolve validation issues before exporting.' : 'Exports a new file; original is not overwritten.'}>Save As…</button></div>
      <details className="raw-source"><summary>Raw .par · loaded source (read-only)</summary><p>{state.document.rawLines.length} raw lines preserved unchanged.</p><p>Original loaded file; edits remain in the GUI working copy. Source line numbers are shown below.</p><pre tabIndex={0}>{state.document.raw.split(/\r\n|\n|\r/).map((line,i)=>`${i+1}  ${line}`).join('\n')}</pre></details>
      </>}
    </div>
  </aside>;
}
