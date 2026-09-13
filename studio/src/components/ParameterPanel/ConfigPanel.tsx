import { useRef, useState } from 'react';
import { createLatestRequest, selectedFile } from '../../data/latestRequest';
import { ConfigControl } from './ConfigControl';
import { parSchema } from '../../data/parSchema';
import { effectiveEntries } from '../../data/ParDocument';
import { loadPar, editPar, parStatus, parErrors, revertPar, exportPar } from '../../state/parState';
import type { ParState } from '../../state/parState';
export function ConfigPanel({ onEdit }: { onEdit: () => void }) {
  const [state,setState] = useState<ParState|null>(null);
  const [message,setMessage] = useState('Open a local ARCH .par file.');
  const latest=useRef(createLatestRequest());
  const errors=state ? parErrors(state) : {};
  function renderEntry(entry: ReturnType<typeof effectiveEntries>[number]) {
    if (!state) return null;
    return <label className="parameter-field" key={entry.key}><span>{entry.key || '(empty key)'} · line {entry.line}{state.document.entries.filter(e=>e.key===entry.key).length>1 ? ' (last occurrence; effective value)' : ''}</span><ConfigControl name={entry.key || 'Empty key'} value={state.changes[entry.key] ?? entry.value} meta={parSchema[entry.key]} error={errors[entry.key]} onChange={value => {setState(editPar(state,entry.key,value));onEdit();}} />{errors[entry.key] && <small className="validation-error">{errors[entry.key]}</small>}</label>;
  }
  return <aside className="parameter-panel panel" aria-label="Real configuration">
    <div className="panel-heading"><h2>Parameters</h2><span>REAL CONFIG</span></div>
    <div className="parameter-scroll">
      <label>Open Config…<input type="file" accept=".par" aria-label="Open Config" onChange={async e => {
        const file=selectedFile(e.target.files); e.target.value=''; if(!file)return;
        setState(null);setMessage('Opening…');
        await latest.current(async () => {
          if(file.size>1024*1024)throw new Error('Config size limit: 1 MiB.');
          const raw=new TextDecoder('utf-8',{fatal:true,ignoreBOM:true}).decode(await file.arrayBuffer());
          return loadPar(file.name,raw);
        }, next => {setState(next);setMessage('Loaded read-only.');}, error => setMessage(error instanceof Error ? error.message : 'Could not read config.'));
      }} /></label>
      <p role="status">{message}</p>
      {state && <><p>{state.filename}</p><p aria-label="Config status">Config: {parStatus(state)}</p>{Object.keys(errors).length>0 && <ul role="alert">{Object.entries(errors).map(([key,error])=><li key={key}>{key}: {error}</li>)}</ul>}
      {(['Grid','EOS','Network','Runtime'] as const).map(group => <section className="parameter-group core-group" key={group}><h3>{group}</h3><div className="group-content">{effectiveEntries(state.document).filter(entry => parSchema[entry.key]?.group === group).map(renderEntry)}</div></section>)}
      <details className="parameter-group custom-group"><summary>Custom Params</summary><div className="group-content">{effectiveEntries(state.document).filter(entry => !parSchema[entry.key]).map(renderEntry)}</div></details>
      <div className="config-actions"><button disabled={parStatus(state)==='saved'} onClick={()=>{setState(revertPar(state));setMessage('Reverted to loaded file.');onEdit();}}>Revert</button>
      <button disabled={parStatus(state)==='invalid'} onClick={()=>{
        const result=exportPar(state); const url=URL.createObjectURL(new Blob([result.text],{type:'text/plain;charset=utf-8'}));
        const link=document.createElement('a');link.href=url;link.download=result.filename;link.click();
        setTimeout(()=>URL.revokeObjectURL(url),60000);setMessage(`Download requested: ${result.filename}. Original unchanged; working copy retained.`);
      }}>Save As…</button><button disabled title="No writable file handle">Save in place unavailable</button></div>
      <p>{state.document.rawLines.length} raw lines preserved unchanged.</p></>}
    </div><div className="panel-note">Real config working copy · original file unchanged. Mock preview is not connected to ARCH initializer.</div>
  </aside>;
}
