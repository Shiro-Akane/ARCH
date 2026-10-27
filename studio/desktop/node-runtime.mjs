/**
 * Select a supported Linux Node runtime for the Host process.
 * An explicit runtime is validated and must fail visibly when invalid. Otherwise
 * the packaged runtime and declared local candidates are inspected in order;
 * this selection does not install packages or change the scientific environment.
 */
import path from 'node:path';

export async function selectLinuxNode({explicit, bundled, searchPath = '', home, probe}) {
 if (explicit && (!path.isAbsolute(explicit) || explicit.includes('\0'))) {
  throw new Error('ARCH_STUDIO_NODE must name an absolute Linux Node executable.');
 }
 const candidates = explicit ? [explicit] : [
  ...(bundled ? [bundled] : []),
  ...searchPath.split(path.delimiter).filter(Boolean).map(dir => path.resolve(dir, 'node')),
  path.join(home, '.local/opt/node-studio/bin/node'), '/usr/bin/node',
 ];
 for (const candidate of new Set(candidates)) {
  try {
   const version = await probe(candidate);
   if (Number(version.match(/^v(\d+)\./)?.[1]) >= 24) return candidate;
  } catch { /* Inspect the next runtime without executing a shell. */ }
 }
 throw new Error(explicit
  ? 'ARCH_STUDIO_NODE does not provide Linux Node 24+. Check the selected executable.'
  : 'Linux Node 24+ missing. Put it on PATH or set ARCH_STUDIO_NODE to its absolute path.');
}

/** A Windows Host needs native Node, not Electron or a WSL executable shim. */
export async function selectWindowsNode({explicit,bundled,searchPath='',probe}){
 const absolute=value=>typeof value==='string'&&/^[A-Za-z]:[\\/]/.test(value)&&!value.includes('\0')&&value.toLowerCase().endsWith('.exe');
 if(explicit&&!absolute(explicit))throw new Error('ARCH_STUDIO_NODE must name an absolute Windows Node .exe.');
 const candidates=explicit?[explicit]:[...(bundled?[bundled]:[]),...searchPath.split(';').filter(Boolean).map(dir=>path.win32.join(dir,'node.exe'))];
 for(const candidate of new Set(candidates)){
  if(!absolute(candidate))continue;
  try{
   const version=JSON.parse(await probe(candidate));
   if(version.platform==='win32'&&version.arch==='x64'&&!version.electron&&Number(version.node?.match(/^(\d+)\./)?.[1])>=24)return candidate;
  }catch{/* Inspect the next declared native runtime without a shell. */}
 }
 throw new Error(explicit?'ARCH_STUDIO_NODE does not provide native Windows x64 Node 24+.':'Native Windows x64 Node 24+ missing. Put it on PATH or set ARCH_STUDIO_NODE.');
}
