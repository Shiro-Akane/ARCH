/**
 * Assemble a native x64 desktop from locked production assets.
 * Workflow: validate the host and Electron version sources, recreate only the
 * owned staging tree, copy renderer/Host/runtime and license files, then package
 * Electron. Tests, historical reports and scientific outputs are excluded. The
 * Core and system GUI/terminal requirements stay external.
 */
import {mkdir,cp,writeFile,readFile,rm,access} from 'node:fs/promises';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

const PACKAGE_NAME='arch-studio';
const EXACT_VERSION=/^(?:0|[1-9]\d*)\.(?:0|[1-9]\d*)\.(?:0|[1-9]\d*)$/;

export function requireExactVersion(value,source){
 if(typeof value!=='string'||!EXACT_VERSION.test(value))
  throw new Error(`${source} must be an exact x.y.z version; received ${JSON.stringify(value)}.`);
 return value;
}

export function validateElectronVersion({manifest,lockfile,installedManifest}){
 const version=requireExactVersion(manifest?.devDependencies?.electron,'package.json devDependencies.electron');
 const lockRoot=requireExactVersion(lockfile?.packages?.['']?.devDependencies?.electron,'package-lock.json root devDependencies.electron');
 if(lockRoot!==version)
  throw new Error(`Electron version mismatch: package.json has ${version}, but package-lock.json root has ${lockRoot}.`);

 const lockedVersion=requireExactVersion(lockfile?.packages?.['node_modules/electron']?.version,'package-lock.json node_modules/electron.version');
 if(lockedVersion!==version)
  throw new Error(`Electron version mismatch: package.json has ${version}, but package-lock.json node_modules/electron has ${lockedVersion}.`);

 const installedVersion=requireExactVersion(installedManifest?.version,'node_modules/electron/package.json version');
 if(installedVersion!==version)
  throw new Error(`Electron version mismatch: package.json has ${version}, but installed Electron is ${installedVersion}.`);
 return version;
}

function requireSupportedPlatform(platform){
 if(platform!=='linux'&&platform!=='win32')
  throw new Error(`Unsupported desktop platform ${JSON.stringify(platform)}; use native Linux or Windows.`);
 return platform;
}

export function assertSupportedEnvironment({platform,arch,nodeVersion,electronVersion}){
 requireSupportedPlatform(platform);
 if(electronVersion)throw new Error('Package with standalone Node; Electron cannot be the bundled Host runtime.');
 if(arch!=='x64')
  throw new Error(`Unsupported desktop architecture ${JSON.stringify(arch)}; only x64 is supported.`);
 const match=/^(\d+)\.(\d+)\.(\d+)(?:[-+].*)?$/.exec(String(nodeVersion??''));
 if(!match||Number(match[1])<24)
  throw new Error(`Desktop packaging requires Node 24+; found ${JSON.stringify(nodeVersion)}.`);
 return {platform,arch,nodeVersion};
}

export function desktopLayout(platform){
 requireSupportedPlatform(platform);
 if(platform==='win32')return {
  runtimeFile:'node.exe',
  packagedExecutable:'arch-studio-bin.exe',
  linuxWrapper:null,
 };
 return {
  runtimeFile:'node',
  packagedExecutable:'arch-studio-bin',
  linuxWrapper:'arch-studio',
 };
}

async function readJsonFile(file,label){
 let text;
 try{
  text=await readFile(file,'utf8');
 }catch(error){
  throw new Error(`${label} is missing at ${file}.`,{cause:error});
 }
 try{
  const value=JSON.parse(text);
  if(!value||typeof value!=='object'||Array.isArray(value))
   throw new Error('expected a JSON object');
  return value;
 }catch(error){
  throw new Error(`${label} at ${file} is invalid: ${error.message}`,{cause:error});
 }
}

export async function packageDesktop({base=process.cwd()}={}){
 const platform=process.platform;
 const arch=process.arch;
 assertSupportedEnvironment({platform,arch,nodeVersion:process.versions.node,electronVersion:process.versions.electron});

 const manifest=await readJsonFile(path.join(base,'package.json'),'package.json');
 if(manifest.name!==PACKAGE_NAME)
  throw new Error('Package ARCH Studio from its studio directory.');
 const lockfile=await readJsonFile(path.join(base,'package-lock.json'),'package-lock.json');
 const installedManifest=await readJsonFile(path.join(base,'node_modules','electron','package.json'),'Installed Electron metadata');
 const electronVersion=validateElectronVersion({manifest,lockfile,installedManifest});
 const checksums=await readJsonFile(path.join(base,'node_modules','electron','checksums.json'),'Locked Electron checksums');
 let nodeLicense;
 for(const directory of [path.dirname(process.execPath),path.resolve(path.dirname(process.execPath),'..')]){
  const candidate=path.join(directory,'LICENSE');
  try{await access(candidate);nodeLicense=candidate;break;}catch(error){if(error.code!=='ENOENT')throw error;}
 }
 if(!nodeLicense)throw new Error('Keep the official Node distribution LICENSE next to node.exe or above bin/node before packaging.');

 const layout=desktopLayout(platform);
 const {packager}=await import('@electron/packager');
 const staging=path.join(base,'.local','desktop-app');
 const outputRoot=path.join(base,'.local','desktop-release');

 await rm(staging,{recursive:true,force:true});
 await mkdir(staging,{recursive:true});
 for(const file of ['main.cjs','close-assets.mjs','preload.cjs','arguments.mjs','node-runtime.mjs','launcher.html','launcher.js','launcher.css'])
  await cp(path.join(base,'desktop',file),path.join(staging,file));
 for(const dir of ['dist','host','src'])
  await cp(path.join(base,dir),path.join(staging,dir),{recursive:true,filter:source=>!source.endsWith('.md')});

 // Host Plotfile workers decode HDF5 outside the frontend bundle.
 await mkdir(path.join(staging,'node_modules'),{recursive:true});
 await cp(path.join(base,'node_modules','h5wasm'),path.join(staging,'node_modules','h5wasm'),{recursive:true});
 await mkdir(path.join(staging,'runtime'),{recursive:true});
 await cp(process.execPath,path.join(staging,'runtime',layout.runtimeFile));
 await cp(nodeLicense,path.join(staging,'runtime','LICENSE.node'));
 await writeFile(path.join(staging,'package.json'),JSON.stringify({name:PACKAGE_NAME,version:process.env.ARCH_STUDIO_VERSION??manifest.version,type:'module',main:'main.cjs',description:'ARCH Studio desktop with local Host',author:'ARCH Studio',license:'MIT'}));
 await cp(path.join(base,'..','LICENSE'),path.join(staging,'LICENSE'));
 await cp(path.join(base,'DEPENDENCY-LICENSES.json'),path.join(staging,'DEPENDENCY-LICENSES.json'));

 const output=await packager({
  dir:staging,
  out:outputRoot,
  name:'arch-studio-bin',
  platform,
  arch,
  electronVersion,
  // Reuse the official installer's checksums and optional offline cache.
  download:{checksums,...(process.env.electron_config_cache?{cacheRoot:process.env.electron_config_cache}:{})},
  overwrite:true,
  asar:false,
  prune:false,
 });
 for(const destination of output){
  await access(path.join(destination,layout.packagedExecutable));
  if(layout.linuxWrapper)
   await cp(path.join(base,'desktop',layout.linuxWrapper),path.join(destination,layout.linuxWrapper));
  await cp(path.join(base,'desktop','README.md'),path.join(destination,'README.md'));
  await cp(path.join(base,'DEPENDENCIES.md'),path.join(destination,'DEPENDENCIES.md'));
  await cp(path.join(base,'..','LICENSE'),path.join(destination,'LICENSE.arch'));
 }
 console.log(output);
}

if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url)){
 try{
  await packageDesktop();
 }catch(error){
  console.error(error instanceof Error?error.message:error);
  process.exitCode=1;
 }
}
