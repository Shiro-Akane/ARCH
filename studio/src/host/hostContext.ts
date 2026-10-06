import {createContext,useContext} from 'react';import type {ProjectSnapshot} from './contracts';
export const HostContext=createContext<{snapshot:ProjectSnapshot|null;connected:boolean;update:(snapshot:ProjectSnapshot)=>void;failed:()=>void}>({snapshot:null,connected:false,update:()=>{},failed:()=>{}});
export const useHost=()=>useContext(HostContext);
