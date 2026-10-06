import {createContext,useContext,useEffect,useRef,useState} from 'react';
import type {ReactNode} from 'react';
import {useHost} from './hostContext';
import {BuildProjectRefresh} from './BuildProjectRefresh';
import {HttpLocalHostAdapter} from './LocalHostAdapter';
import {buildRequest,validateBuildSnapshot,validateBuildEvents} from './BuildAdapter';
import type {BuildSnapshot,BuildEvents} from './contracts';
interface BuildContextValue {status:BuildSnapshot|undefined;log:BuildEvents|undefined;error:string;pending:boolean;start:()=>Promise<void>;clear:{buildId:string;sequence:number};clearView:()=>void}
const Context=createContext<BuildContextValue|null>(null);
export function BuildProvider({children}:{children:ReactNode}){
 const host=useHost();const {snapshot,connected}=host;const projectId=snapshot?.session.projectId;
 const latestHost=useRef(host);useEffect(()=>{latestHost.current=host;},[host]);
 const [status,setStatus]=useState<BuildSnapshot>();const [log,setLog]=useState<BuildEvents>();const [error,setError]=useState('');const [pending,setPending]=useState(false);const [clear,setClear]=useState({buildId:'',sequence:0});const generation=useRef(0);
 useEffect(()=>{const gen=++generation.current;let timer:ReturnType<typeof setTimeout>;let gone=false;const projectRefresh=new BuildProjectRefresh();const adapter=new HttpLocalHostAdapter();
  async function poll(){if(!projectId||!connected)return;try{const next=validateBuildSnapshot(await buildRequest('/api/build/status'),projectId);if(gone||gen!==generation.current)return;setStatus(next);const id=next.activeBuildId??next.latestResult?.buildId;if(id){const events=validateBuildEvents(await buildRequest('/api/build/'+id+'/events'),projectId,id);if(!gone&&gen===generation.current)setLog(events);}const before=latestHost.current.snapshot;
   await projectRefresh.observe(next,before,()=>adapter.refresh(),()=>!gone&&gen===generation.current&&latestHost.current.snapshot===before,s=>latestHost.current.update(s));
   if(!gone&&gen===generation.current)setError('');}catch(e){if(!gone)setError(e instanceof Error?e.message:'Build unavailable');}finally{if(!gone)timer=setTimeout(()=>void poll(),1000);}}
  void poll();return()=>{gone=true;clearTimeout(timer);};
 },[projectId,connected]);
 const s=status?.projectId===projectId?status:undefined;const p=s?.profile;const currentLog=log&&log.projectId===projectId&&(log.buildId===s?.activeBuildId||log.buildId===s?.latestResult?.buildId)?log:undefined;
 async function start(){if(!projectId||!p||pending)return;setPending(true);setError('');const gen=generation.current;try{await buildRequest('/api/build',{projectId,profileId:p.id});if(gen!==generation.current)return;setClear({buildId:'',sequence:0});setStatus(validateBuildSnapshot(await buildRequest('/api/build/status'),projectId));}catch(e){if(gen===generation.current)setError(e instanceof Error?e.message:'Build failed');}finally{setPending(false);}}

 return <Context.Provider value={{status:s,log:currentLog,error,pending,start,clear,clearView:()=>setClear({buildId:currentLog?.buildId??'',sequence:currentLog?.lastSequence??0})}}>{children}</Context.Provider>;
}
// eslint-disable-next-line react-refresh/only-export-components
export function useBuild(){const value=useContext(Context);if(!value)throw new Error('Build provider missing');return value;}
export function BuildOutput(){
 const {log,clear,clearView}=useBuild();
 return <>{log?.truncated&&<p>Earlier build output truncated in Studio view.</p>}<button onClick={clearView}>Clear view</button><pre className="build-log" tabIndex={0}>{log?.events.filter(e=>log.buildId!==clear.buildId||e.sequence>clear.sequence).map(e=>'['+e.sequence+' '+e.kind+'] '+(e.text??e.state??'')).join('\n')??'No build output.'}</pre></>;
}
