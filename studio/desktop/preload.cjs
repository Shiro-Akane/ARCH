/**
 * Expose the fixed desktop bootstrap through Electron's isolated preload bridge.
 * The renderer receives selected launch/connection information rather than Node
 * process access. Main and Host validate their own identities and requests.
 */
const {contextBridge,ipcRenderer}=require('electron');
const bootstrap=JSON.parse(process.argv.find(x=>x.startsWith('--arch-bootstrap='))?.slice(17)||'{}');
contextBridge.exposeInMainWorld('archDesktop',{
 ...bootstrap,
 pickProject:()=>ipcRenderer.invoke('desktop:pick-project'),
 launch:options=>ipcRenderer.invoke('desktop:launch',options),
 ...(bootstrap.nativeHost?{openPath:()=>ipcRenderer.invoke('desktop:open-path')}:{}),
 savePath:name=>ipcRenderer.invoke('desktop:save-path',name),
 setDirty:value=>ipcRenderer.send('desktop:dirty',value===true)
});
