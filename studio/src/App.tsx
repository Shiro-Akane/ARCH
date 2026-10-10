import {BuildProvider} from './host/BuildProvider';
import {WorkflowProvider} from './state/workflowContext';
import {WorkflowBar} from './components/WorkflowBar';
import {initialWorkspace} from './data/workspacePresentation';
import {WorkspaceHeader} from './components/WorkspaceHeader';
import {ConfigurationBridge,ConfigurationIdentity} from './components/ConfigurationBridge';
import {CoreParameterProvider} from './state/coreParameters';
import {RealInitWorkspace} from './components/RealInitWorkspace';
import type {WorkingCopy} from './data/RealInitPreviewProvider';
import {LocalHostProvider} from './host/LocalHostProvider';
import { ProjectPanel } from './host/ProjectPanel';
import { modeDescriptions } from './data/modePresentation';
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
  const [copy,setCopy]=useState<WorkingCopy|null>(null);
  const [source, setSource] = useState(()=>initialWorkspace(typeof window==='undefined'?'':window.location.search));
  const debugSamples=source==='mock'||source==='cellular';
  const sampleView = source === 'cellular';
  const realView = source === 'plotfile';
  const configView = source === 'config';
  return <LocalHostProvider><BuildProvider><WorkflowProvider><CoreParameterProvider><div className="studio-shell" onKeyDown={e=>{if(source!=='mock')return;const action=historyShortcut(e.key,e.ctrlKey,e.metaKey,e.shiftKey);if(action){e.preventDefault();dispatch({type:'history/restore',working:mockHistory.current[action](state.working)});}}} onPointerDownCapture={e=>{if(source==='mock' && e.target instanceof HTMLInputElement && (e.target.type==='range'||e.target.dataset.numeric==='true'))mockHistory.current.begin(state.working);}} onPointerUp={()=>mockHistory.current.end()} onPointerCancel={()=>mockHistory.current.end()}>
    <WorkspaceHeader source={source} copy={copy} debugSamples={debugSamples} onSource={next=>{setSource(next);if(next!==source)dispatch({type:'config/external-edit'});}} />
    <ConfigurationBridge copy={copy}/>{configView&&<ConfigurationIdentity copy={copy}/>}
    <ProjectPanel />
    <p className="mode-description" hidden={!debugSamples}>{modeDescriptions[source]}</p>
    <div hidden={source !== 'mock' && !configView} className="mock-shell"><main id="workspace" className="workspace"><div style={{display:configView ? 'contents' : 'none'}}><ConfigPanel onWorkingCopy={setCopy} active={configView} onInspect={setParameter} onEdit={() => dispatch({type:'config/external-edit'})} /></div>{!configView && <ParameterPanel preview={state.preview} invalid={state.config==='invalid'} onPreview={()=>dispatch({type:'preview/start',revision:state.revision})} parameters={state.working} onEdit={(key, value) => {mockHistory.current.record(state.working,{...state.working,[key]:value});dispatch({ type: 'edit', key, value });}} />}{!configView && <Preview previousConfig={false} onGenerate={()=>dispatch({type:'preview/start',revision:state.revision})} state={state} onField={field => dispatch({ type: 'field/select', field })} onPoint={(x,y) => dispatch({ type: 'point/select', x, y })} />}<RealInitWorkspace copy={copy} active={configView} parameter={parameter} onSample={()=>setParameter(null)} />{!configView && <Inspector state={state} />}</main>
    {configView&&<WorkflowBar copy={copy}/>}{!configView && <StatusBar state={state} onPreview={() => dispatch({ type: 'preview/start', revision: state.revision })} onSave={() => dispatch({ type: 'config/save' })} onRevert={() => {mockHistory.current.reset();dispatch({ type: 'config/revert' });}} />}</div>
    {sampleView && <CellularSample />}
    {realView && <PlotfileWorkspace />}
  </div></CoreParameterProvider></WorkflowProvider></BuildProvider></LocalHostProvider>;
}
