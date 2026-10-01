import {useEffect} from 'react';
import {useHost} from './hostContext';
import {useBuild} from './BuildProvider';
export function BuildPanel({onActive}:{onActive:(active:boolean)=>void}){
 const {snapshot,connected}=useHost();
 const {status:s,error,pending,start}=useBuild();const p=s?.profile;
 useEffect(()=>onActive(pending||!!s?.activeBuildId),[pending,s?.activeBuildId,onActive]);
 const m=s?.lastSuccessfulBuild;
 return <section className="build-panel" aria-label="Controlled Build"><h3>Build</h3><p>Managed source root: {p?.managedSourceRoot??snapshot?.session.projectRoot}</p>{p&&<details><summary>Build Profile · {p.displayName}</summary><dl><dt>Profile ID</dt><dd>{p.id}</dd><dt>Build directory</dt><dd>{p.managedSourceRoot}/{p.buildDirRelative}</dd><dt>Target</dt><dd>{p.target}</dd><dt>Expected executable</dt><dd>{p.managedSourceRoot}/{p.outputBinaryRelative}</dd><dt>Registered case ID (configured)</dt><dd>{p.caseId??'unknown'} · unified ARCH executable</dd><dt>Parallel jobs</dt><dd>{p.parallelism}</dd><dt>Tracked inputs (incomplete dependency coverage)</dt><dd>{p.trackedInputs.join(', ')}</dd></dl></details>}
 <p aria-label="Build state">Build: {s?.state??'not-configured'}</p>{s?.reason&&<p>{s.reason}</p>}<button disabled={!connected||!snapshot?.host.capabilities.build||!s?.configured||!!s.activeBuildId||pending} onClick={()=>void start()}>{pending||s?.activeBuildId?'Building…':'Build'}</button>
 {error&&<p role="alert">{error}</p>}{s?.latestResult?.error&&<p role="alert">{s.latestResult.error}</p>}
 <p>Mapping: {s?.mappingState??'unknown'} · not independently verified by Core</p><p aria-label="Binary build state">Binary: {s?.binaryState??'freshness-unknown'}</p><p>{s?.freshnessReason}</p>{!!s?.changedInputs.length&&<p>Changed tracked inputs: {s.changedInputs.join(', ')}</p>}
 {m&&<details><summary>Last successful Build · {m.finishedAt}</summary><dl><dt>Build ID</dt><dd>{m.buildId}</dd><dt>Managed source root</dt><dd>{m.managedSourceRoot}</dd><dt>Source Git HEAD</dt><dd>{m.sourceGitHead??'unknown'}</dd><dt>Repository dirty at build</dt><dd>{m.repositoryDirty===undefined?'unknown':String(m.repositoryDirty)} (separate from tracked-input freshness)</dd><dt>Binary SHA-256</dt><dd>{m.outputBinary.fingerprint.sha256}</dd><dt>Profile fingerprint</dt><dd>{m.buildProfileFingerprint}</dd></dl></details>}
 <p>Build output is available in the workspace terminal drawer.</p><p>Build does not save config, run simulation or generate Preview.</p></section>;
}
