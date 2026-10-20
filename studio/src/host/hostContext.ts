/**
 * Expose the typed local Host context to React consumers.
 * The provider owns snapshot refresh; this hook only requires the caller to be
 * inside that provider and introduces no process or filesystem lifetime.
 */
import {createContext,useContext} from 'react';import type {ProjectSnapshot} from './contracts';
export const HostContext=createContext<{snapshot:ProjectSnapshot|null;connected:boolean;update:(snapshot:ProjectSnapshot)=>void;failed:()=>void}>({snapshot:null,connected:false,update:()=>{},failed:()=>{}});
export const useHost=()=>useContext(HostContext);
