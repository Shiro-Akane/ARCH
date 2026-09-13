import { modeDescriptions } from './data/modePresentation';
import { ParameterInspector } from './components/Inspector/ParameterInspector';
import type { ParameterDetails } from './components/Inspector/ParameterInspector';
import { ConfigPanel } from './components/ParameterPanel/ConfigPanel';
import { PlotfileWorkspace } from './components/PlotfileWorkspace';
import { EditHistory, historyShortcut } from './state/editHistory';
import type { Parameters } from './state/studioState';
import { useRef, useState } from 'react';
import { CellularSample } from './components/CellularSample';
import { useStudio } from './state/useStudio';
import { ParameterPanel } from './components/ParameterPanel/ParameterPanel';
import { Preview } from './components/Preview/Preview';
import { Inspector } from './components/Inspector/Inspector';
import { StatusBar } from './components/StatusBar/StatusBar';

export default function App() {
  const { state, dispatch } = useStudio();
  const mockHistory=useRef(new EditHistory<Parameters>());
  const [parameter,setParameter]=useState<ParameterDetails|null>(null);
  const [source, setSource] = useState('mock');
  const sampleView = source === 'cellular';
  const realView = source === 'plotfile';
  const configView = source === 'config';
  return <div className="studio-shell" onKeyDown={e=>{if(source!=='mock')return;const action=historyShortcut(e.key,e.ctrlKey,e.metaKey,e.shiftKey);if(action){e.preventDefault();dispatch({type:'history/restore',working:mockHistory.current[action](state.working)});}}} onPointerDownCapture={e=>{if(source==='mock' && e.target instanceof HTMLInputElement && (e.target.type==='range'||e.target.dataset.numeric==='true'))mockHistory.current.begin(state.working);}} onPointerUp={()=>mockHistory.current.end()} onPointerCancel={()=>mockHistory.current.end()}>
    <header className="topbar">
      <div className="brand" aria-label="ARCH Studio"><svg width="27" height="28" viewBox="0 0 27 28" aria-hidden="true"><path d="M3 23 13.5 4 24 23h-7l-3.5-7-3.5 7Z" fill="currentColor" /></svg><span>ARCH<span className="brand-light">STUDIO</span></span></div>
      <span className="header-divider" />
      <div className="document-meta"><span className="meta-label">CASE</span><strong>{configView ? 'Real config' : realView ? 'Plotfile' : sampleView ? 'CellularDet' : 'Hotspot demo'}</strong><span className="meta-slash">/</span><span className="meta-label">CONFIG</span><span>{configView ? 'Working copy' : realView ? 'Not applicable' : sampleView ? 'Cellular.par · snapshot' : 'Untitled'}</span></div>
      <div className="topbar-right"><span className="demo-badge">{configView ? 'REAL CONFIG' : realView ? 'REAL PLOTFILE' : sampleView ? 'REAL DATA' : 'MOCK / DEMO'}</span><select aria-label="Workspace sample" value={source} onChange={e=>{setSource(e.target.value);if(e.target.value!==source)dispatch({type:'config/external-edit'});}}><option value="config">Real Config</option><option value="plotfile">Real Plotfile</option><option value="cellular">Cellular · real result</option><option value="mock">Hotspot · Mock demo</option></select><span className="shell-badge">{configView ? 'In memory' : source !== 'mock' ? 'Read only' : `Config: ${state.config}`}</span></div>
    </header>
    <p className="mode-description">{modeDescriptions[source]}</p>
    <div hidden={source !== 'mock' && !configView} className="mock-shell"><main id="workspace" className="workspace"><div style={{display:configView ? 'contents' : 'none'}}><ConfigPanel active={configView} onInspect={setParameter} onEdit={() => dispatch({type:'config/external-edit'})} /></div>{!configView && <ParameterPanel preview={state.preview} invalid={state.config==='invalid'} onPreview={()=>dispatch({type:'preview/start',revision:state.revision})} parameters={state.working} onEdit={(key, value) => {mockHistory.current.record(state.working,{...state.working,[key]:value});dispatch({ type: 'edit', key, value });}} />}<Preview previousConfig={configView} onGenerate={()=>dispatch({type:'preview/start',revision:state.revision})} state={state} onField={field => dispatch({ type: 'field/select', field })} onPoint={(x,y) => dispatch({ type: 'point/select', x, y })} />{configView ? <ParameterInspector parameter={parameter} /> : <Inspector state={state} />}</main>
    {!configView && <StatusBar state={state} onPreview={() => dispatch({ type: 'preview/start', revision: state.revision })} onSave={() => dispatch({ type: 'config/save' })} onRevert={() => {mockHistory.current.reset();dispatch({ type: 'config/revert' });}} />}</div>
    {sampleView && <CellularSample />}
    {realView && <PlotfileWorkspace />}
  </div>;
}
