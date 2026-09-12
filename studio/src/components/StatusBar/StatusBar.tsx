import type { StudioState } from '../../state/studioState';
import { Icon } from '../Icon';
export function StatusBar({ state, onPreview, onSave, onRevert }: { state: StudioState; onPreview: () => void; onSave: () => void; onRevert: () => void }) {
  return <footer className="status-bar">
    <div className="actions" aria-label="Workspace actions">
      <button disabled title="Build is disconnected in Phase 0"><Icon name="build" />Build</button>
      <span className="action-divider" />
      <button className="preview-action" disabled={state.config === 'invalid' || state.preview === 'generating'} onClick={onPreview} title={state.config === 'invalid' ? 'Correct invalid parameters first' : 'Generate mock data only'}><Icon name="play" />Preview</button>
      <button disabled={state.config !== 'dirty'} onClick={onSave} title="Save working copy in demo memory only"><Icon name="save" />Save</button>
      <button disabled={state.config === 'saved'} onClick={onRevert} title="Restore the most recent Demo Save"><Icon name="reset" />Revert</button>
      <span className="action-divider" />
      <button disabled title="Run control is disconnected in Phase 0"><Icon name="play" />Start</button>
      <button disabled title="Monitoring is disconnected in Phase 0"><Icon name="monitor" />Monitor</button>
    </div>
    <div className="status-right"><span className="small-dot" /><span>Solver disconnected</span><span className="version-label">PHASE 0 · M5</span></div>
  </footer>;
}
