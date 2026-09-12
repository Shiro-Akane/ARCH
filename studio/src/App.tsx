import { useState } from 'react';
import { CellularSample } from './components/CellularSample';
import { useStudio } from './state/useStudio';
import { ParameterPanel } from './components/ParameterPanel/ParameterPanel';
import { Preview } from './components/Preview/Preview';
import { Inspector } from './components/Inspector/Inspector';
import { StatusBar } from './components/StatusBar/StatusBar';

export default function App() {
  const { state, dispatch } = useStudio();
  const [sampleView, setSampleView] = useState(false);
  return <div className="studio-shell">
    <header className="topbar">
      <a className="brand" href="#workspace" aria-label="ARCH Studio workspace"><svg width="27" height="28" viewBox="0 0 27 28" aria-hidden="true"><path d="M3 23 13.5 4 24 23h-7l-3.5-7-3.5 7Z" fill="currentColor" /></svg><span>ARCH<span className="brand-light">STUDIO</span></span></a>
      <span className="header-divider" />
      <div className="document-meta"><span className="meta-label">CASE</span><strong>{sampleView ? 'CellularDet' : 'Hotspot demo'}</strong><span className="meta-slash">/</span><span className="meta-label">CONFIG</span><span>{sampleView ? 'Cellular.par · snapshot' : 'Untitled'}</span></div>
      <div className="topbar-right"><span className="demo-badge">{sampleView ? 'REAL DATA' : 'DEMO'}</span><select aria-label="Workspace sample" value={sampleView ? 'cellular' : 'mock'} onChange={e=>setSampleView(e.target.value === 'cellular')}><option value="cellular">Cellular · real result</option><option value="mock">Hotspot · Mock demo</option></select><span className="shell-badge">{sampleView ? 'Read only' : `Config: ${state.config}`}</span></div>
    </header>
    <div hidden={sampleView} className="mock-shell"><main id="workspace" className="workspace"><ParameterPanel parameters={state.working} onEdit={(key, value) => dispatch({ type: 'edit', key, value })} /><Preview state={state} onField={field => dispatch({ type: 'field/select', field })} onPoint={(x,y) => dispatch({ type: 'point/select', x, y })} /><Inspector state={state} /></main>
    <StatusBar state={state} onPreview={() => dispatch({ type: 'preview/start', revision: state.revision })} onSave={() => dispatch({ type: 'config/save' })} onRevert={() => dispatch({ type: 'config/revert' })} /></div>
    {sampleView && <CellularSample />}
  </div>;
}
