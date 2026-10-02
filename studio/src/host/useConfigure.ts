import {useEffect,useRef,useState} from 'react';
import {useHost} from './hostContext';
import {buildRequest,validateBuildEvents} from './BuildAdapter';
import {validateConfigureStatus} from './ConfigureAdapter';
import type {ConfigureStatus} from './ConfigureAdapter';
import type {BuildEvents} from './contracts';
export function useConfigure(){
 const {snapshot,connected}=useHost(),projectId=snapshot?.session.projectId;
 const [status,setStatus]=useState<ConfigureStatus>(),[log,setLog]=useState<BuildEvents>(),[error,setError]=useState('');
 const [pending,setPending]=useState(false),generation=useRef(0);
 useEffect(()=>{
  const epoch=++generation.current;let gone=false,timer:ReturnType<typeof setTimeout>;
  async function poll(){
   if(!connected||!projectId)return;
   try{
    const next=validateConfigureStatus(await buildRequest('/api/configure/status'),projectId);
    if(gone||generation.current!==epoch)return;setStatus(next);
    if(next.operationId){
     const events=validateBuildEvents(await buildRequest('/api/configure/'+next.operationId+'/events'),projectId,next.operationId);
     if(!gone&&generation.current===epoch)setLog(events);
    }
    if(!gone&&generation.current===epoch)setError('');
   }catch(e){if(!gone&&generation.current===epoch)setError(e instanceof Error?e.message:'Configure unavailable');}
   finally{if(!gone)timer=setTimeout(()=>void poll(),1000);}
  }
  void poll();return()=>{gone=true;clearTimeout(timer);};
 },[projectId,connected]);
 const current=connected&&status?.projectId===projectId?status:undefined;
 async function action(cancel=false){
  if(!current?.profileId||!projectId||pending)return;
  const epoch=generation.current;setPending(true);setError('');
  try{
   const next=cancel&&current.operationId
    ?await buildRequest('/api/configure/'+current.operationId+'/cancel',undefined,'POST')
    :await buildRequest('/api/configure',{projectId,profileId:current.profileId});
   const checked=validateConfigureStatus(next,projectId);
   if(epoch===generation.current)setStatus(checked);
  }catch(e){if(epoch===generation.current)setError(e instanceof Error?e.message:'Configure failed');}
  finally{setPending(false);}
 }
 return {status:current,error:connected?error:'',pending,start:()=>action(),cancel:()=>action(true),
  log:log?.projectId===projectId&&log?.buildId===current?.operationId?log:undefined};
}
