import {SourceView} from './SourceView';
import {BuildPanel} from './BuildPanel';
import {useRef,useState} from 'react';
import {HttpLocalHostAdapter} from './LocalHostAdapter';
import {useHost} from './hostContext';
import type {ProjectFileRef} from './contracts';
function FileIdentity({label,file,state}:{label:string;file:ProjectFileRef|undefined;state:string}) {
 return <div className="project-file"><strong>{label}</strong><span>{file?.relativePath ?? 'Not configured'}</span><span>{state}{file?.changed ? ' · fingerprint changed since session opened' : ''}</span>{file?.error && <p role="alert">{file.error}</p>}{file?.sha256 && <details><summary>Fingerprint</summary><dl><dt>SHA-256</dt><dd>{file.sha256}</dd><dt>Bytes</dt><dd>{file.size}</dd><dt>Modified</dt><dd>{file.modifiedTime}</dd></dl></details>}</div>;
}
export function ProjectPanel() {
 const adapter=useRef(new HttpLocalHostAdapter());
 const {snapshot,update,failed}=useHost();
 const [activeBuild,setActiveBuild]=useState(false);const [busy,setBusy]=useState(false);const [error,setError]=useState('');
 async function request(refresh:boolean){if(busy)return;setBusy(true);setError('');try{update(await (refresh?adapter.current.refresh():adapter.current.connect()));}catch(e){failed();setError(e instanceof Error?e.message:'Local Host request failed');}finally{setBusy(false);}}
 const s=snapshot?.session;
 return <details className="project-panel"><summary>Project / Local Host · {busy?'Checking…':error?'Connection needs attention':snapshot?'Connected':'Not connected'}{s?` · ${s.displayName}`:''}</summary><div className="project-content"><div className="project-actions"><button disabled={busy||activeBuild} title={activeBuild?'Wait for the active Build before reconnecting.':undefined} onClick={()=>void request(false)}>{snapshot?'Reconnect':'Connect Local Host'}</button><button disabled={busy||!snapshot} onClick={()=>void request(true)}>Refresh Project State</button><span>Config lifecycle · 127.0.0.1:4180</span></div>{error&&<p role="alert">{error}{snapshot?' Showing last known session.':''}</p>}{!snapshot&&<p>Start the Local Host with an explicit project root, then connect. Existing Mock, Config and Plotfile workspaces remain available.</p>}{snapshot&&s&&<><p>Local · Protocol {snapshot.host.protocolVersion} · {snapshot.host.platform}</p><div className="project-files"><FileIdentity label="Case source" file={s.caseSource} state={s.sourceState}/><FileIdentity label="Config" file={s.parameterFile} state={s.configFileState}/><FileIdentity label="Executable" file={s.executable} state={s.binaryState}/></div><p>Case / executable mapping is reported by the Build Profile · Parameter metadata unavailable</p><p>{snapshot.host.capabilities.preview?'Real initial preview is available in Real Config.':'Real initial preview requires a configured Preview Profile and matching successful Build.'} Refresh reports external changes only; the Config working copy is never reloaded.</p><BuildPanel onActive={setActiveBuild}/><SourceView/><details><summary>Project details</summary><dl><dt>Root</dt><dd>{s.projectRoot}</dd><dt>Session</dt><dd>{s.projectId}</dd><dt>Opened</dt><dd>{s.openedAt}</dd><dt>Refreshed</dt><dd>{s.refreshedAt}</dd></dl></details></>}</div></details>;
}
