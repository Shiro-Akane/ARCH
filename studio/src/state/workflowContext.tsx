import {createContext,useContext,useState} from 'react';
import type {ReactNode} from 'react';
export interface PreviewAction {enabled:boolean;state:string;reason:string;generate:()=>void}
const initial:PreviewAction={enabled:false,state:'unavailable',reason:'Open a configuration and connect a configured Host.',generate:()=>{}};
const Context=createContext<{preview:PreviewAction;setPreview:(value:PreviewAction)=>void}>({preview:initial,setPreview:()=>{}});
export function WorkflowProvider({children}:{children:ReactNode}){const [preview,setPreview]=useState(initial);return <Context.Provider value={{preview,setPreview}}>{children}</Context.Provider>;}
// eslint-disable-next-line react-refresh/only-export-components
export function useWorkflow(){return useContext(Context);}
