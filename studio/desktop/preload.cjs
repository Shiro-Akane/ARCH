const {contextBridge,ipcRenderer}=require('electron');
const bootstrap=JSON.parse(process.argv.find(x=>x.startsWith('--arch-bootstrap='))?.slice(17)||'{}');
contextBridge.exposeInMainWorld('archDesktop',{
 ...bootstrap,
 pickProject:()=>ipcRenderer.invoke('desktop:pick-project'),
 launch:options=>ipcRenderer.invoke('desktop:launch',options),
 ...(process.platform==='linux'?{openPath:()=>ipcRenderer.invoke('desktop:open-path')}:{}),
 savePath:name=>ipcRenderer.invoke('desktop:save-path',name),
 setDirty:value=>ipcRenderer.send('desktop:dirty',value===true)
});
