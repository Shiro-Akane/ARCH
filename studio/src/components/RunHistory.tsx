import {useEffect,useState} from 'react';
import {buildRequest} from '../host/BuildAdapter';
import {validateRunHistory} from '../host/RunAdapter';
import type {RunHistoryRecord} from '../host/RunAdapter';
export function RunHistory({projectId,refreshKey}:{projectId?:string;refreshKey?:string}){
 const [result,setResult]=useState<{projectId:string;records:RunHistoryRecord[];error?:string}>();
 const [stopping,setStopping]=useState<string>();
 useEffect(()=>{
  if(!projectId)return;
  let gone=false,timer:ReturnType<typeof setTimeout>;
  async function refresh(){
   try{
    const records=validateRunHistory(await buildRequest('/api/runs'),projectId!);
    if(!gone)setResult({projectId:projectId!,records});
   }catch(e){if(!gone)setResult({projectId:projectId!,records:[],error:e instanceof Error?e.message:'Run history unavailable.'});}
   finally{if(!gone)timer=setTimeout(()=>void refresh(),3000);}
  }
  void refresh();return()=>{gone=true;clearTimeout(timer);};
 },[projectId,refreshKey]);
 async function stop(runId:string){
  setStopping(runId);
  try{await buildRequest('/api/run/'+runId+'/stop',undefined,'POST');}
  catch(e){setResult(previous=>previous&&previous.projectId===projectId?{...previous,error:e instanceof Error?e.message:'Stop request failed.'}:previous);}
  finally{setStopping(undefined);}
 }
 if(!projectId)return null;
 const current=result?.projectId===projectId?result:undefined;
 return <details className="run-history"><summary>Saved run history</summary>
  <p>These records belong to their saved inputs. Closing Studio does not stop a computation.</p>
  {current?.error&&<p role="alert">{current.error}</p>}
  {!current&&<p role="status">Loading saved runs…</p>}
  {current&&!current.error&&!current.records.length&&<p>No saved runs.</p>}
  <div style={{maxHeight:'25vh',overflow:'auto'}}>{current?.records.map(item=><details key={item.runId}>
   <summary>{item.caseId??'Unknown model'} · {item.state?.state??'status unknown'} · {item.runId}</summary>
   <p>Config: {item.configPath??'unavailable'} · Created: {item.createdAt??'unknown'}</p>
   <p>Input SHA-256: <code>{item.configSha??'unavailable'}</code></p>
   {item.diagnostic&&<p role="alert">{item.diagnostic}</p>}
   {item.state?.error&&<p role="alert">{item.state.error}</p>}
   {item.state&&<p>Core PID: {item.state.processId??'not started'} · Exit: {item.state.exitCode??item.state.signal??'pending'}</p>}
   {item.state&&!item.state.finishedAt&&<button disabled={!!stopping} onClick={()=>void stop(item.runId)}>{stopping===item.runId?'Requesting Stop…':'Stop this run'}</button>}
  </details>)}</div>
 </details>;
}
