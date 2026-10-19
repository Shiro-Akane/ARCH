/**
 * Read the bounded Electron preload bridge or select the development Host endpoint.
 * The renderer obtains connection/launch metadata without acquiring Node access.
 * Host still authenticates requests and validates the selected project.
 */
interface DesktopBridge { endpoint?:string;caseId?:string;project?:string;selectedSource?:string;openPath?:()=>Promise<string|null>;savePath:(name:string)=>Promise<string|null>;setDirty:(value:boolean)=>void }
declare global {interface Window {archDesktop?:DesktopBridge}}
export const desktop=typeof window==='undefined'?undefined:window.archDesktop;
export const hostEndpoint=desktop?.endpoint??'http://127.0.0.1:4180';
