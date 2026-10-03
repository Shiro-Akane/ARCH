interface DesktopBridge { endpoint?:string;caseId?:string;project?:string;openPath?:()=>Promise<string|null>;savePath:(name:string)=>Promise<string|null>;setDirty:(value:boolean)=>void }
declare global {interface Window {archDesktop?:DesktopBridge}}
export const desktop=typeof window==='undefined'?undefined:window.archDesktop;
export const hostEndpoint=desktop?.endpoint??'http://127.0.0.1:4180';
