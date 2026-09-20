import {createContext,useContext,useState} from 'react';
import type {ReactNode} from 'react';
import type {RealPreviewResult} from '../host/previewContracts';
import {useRef,useCallback} from 'react';
interface Snapshot {result:RealPreviewResult;text:string}
interface Context {snapshot:Snapshot|null;setSnapshot:(v:Snapshot|null)=>void;candidate:number|null;setCandidate:(v:number|null)=>void;edit:(key:string,value:string)=>void;focus:(key:string)=>void;registerFocus:(fn:((key:string)=>void)|null)=>void;registerEdit:(fn:((key:string,value:string)=>void)|null)=>void}
const CoreContext=createContext<Context|null>(null);
export function CoreParameterProvider({children}:{children:ReactNode}){const [snapshot,setSnapshot]=useState<Snapshot|null>(null);const [candidate,setCandidate]=useState<number|null>(null);const edit=useRef<((key:string,value:string)=>void)|null>(null);const focus=useRef<((key:string)=>void)|null>(null);const registerFocus=useCallback((fn:((key:string)=>void)|null)=>{focus.current=fn;},[]);const focusKey=useCallback((key:string)=>focus.current?.(key),[]);const registerEdit=useCallback((fn:((key:string,value:string)=>void)|null)=>{edit.current=fn;},[]);const applyEdit=useCallback((key:string,value:string)=>edit.current?.(key,value),[]);return <CoreContext.Provider value={{snapshot,setSnapshot,candidate,setCandidate,edit:applyEdit,registerEdit,focus:focusKey,registerFocus}}>{children}</CoreContext.Provider>;}
// Hook and provider share this small state boundary.
// eslint-disable-next-line react-refresh/only-export-components
export function useCoreParameters(){const c=useContext(CoreContext);if(!c)throw new Error('Core parameter context missing');return c;}
