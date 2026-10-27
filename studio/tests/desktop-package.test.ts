import assert from 'node:assert/strict';
import {access,mkdir,mkdtemp,rm,writeFile} from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import {
 assertSupportedEnvironment,
 desktopLayout,
 packageDesktop,
 requireExactVersion,
 validateElectronVersion,
} from '../desktop/package.mjs';

function inputs(electron='44.9.1'){
 return {
  manifest:{devDependencies:{electron}},
  lockfile:{
   packages:{
    '':{devDependencies:{electron}},
    'node_modules/electron':{version:electron},
   },
  },
  installedManifest:{version:electron},
 };
}

test('accepts an exact Electron version shared by all version sources',()=>{
 const value=inputs();
 assert.equal(validateElectronVersion(value),'44.9.1');
 assert.equal(requireExactVersion('0.1.2','test source'),'0.1.2');
});

const rejectedVersions=[
 ['root manifest range','^44.9.1',/package\.json devDependencies\.electron/],
 ['root manifest malformed','44.9',/package\.json devDependencies\.electron/],
 ['root manifest prerelease','44.9.1-beta.1',/package\.json devDependencies\.electron/],
 ['lock root mismatch','44.9.0',/package\.json has 44\.9\.1.*package-lock\.json root has 44\.9\.0/],
 ['lock root range','~44.9.1',/package-lock\.json root devDependencies\.electron/],
 ['lock installed mismatch','44.9.0',/package-lock\.json node_modules\/electron has 44\.9\.0/],
 ['lock installed range','>=44.9.1',/package-lock\.json node_modules\/electron\.version/],
 ['installed mismatch','44.9.0',/installed Electron is 44\.9\.0/],
 ['installed range','44.x',/node_modules\/electron\/package\.json version/],
 ['installed malformed','44.9.1+build',/node_modules\/electron\/package\.json version/],
];

for(const [name,value,expected] of rejectedVersions){
 test(`rejects ${name}`,()=>{
  const exact=inputs();
  if(name==='root manifest range'||name==='root manifest malformed'||name==='root manifest prerelease'){
   exact.manifest.devDependencies.electron=value;
  }else if(name==='lock root mismatch'||name==='lock root range'){
   exact.lockfile.packages[''].devDependencies.electron=value;
  }else if(name==='lock installed mismatch'||name==='lock installed range'){
   exact.lockfile.packages['node_modules/electron'].version=value;
  }else{
   exact.installedManifest.version=value;
  }
  assert.throws(()=>validateElectronVersion(exact),expected);
 });
}

test('accepts native Linux and Windows x64 with Node 24+',()=>{
 assert.deepEqual(
  assertSupportedEnvironment({platform:'linux',arch:'x64',nodeVersion:'24.0.0'}),
  {platform:'linux',arch:'x64',nodeVersion:'24.0.0'},
 );
 assert.doesNotThrow(()=>assertSupportedEnvironment({platform:'win32',arch:'x64',nodeVersion:'25.2.1'}));
});

test('rejects unsupported platforms, architectures and Node versions',()=>{
 assert.throws(()=>assertSupportedEnvironment({platform:'win32',arch:'x64',nodeVersion:'24.22.0',electronVersion:'44.9.1'}),/standalone Node/);
 assert.throws(()=>assertSupportedEnvironment({platform:'darwin',arch:'x64',nodeVersion:'24.0.0'}),/Unsupported desktop platform/);
 assert.throws(()=>assertSupportedEnvironment({platform:'linux',arch:'arm64',nodeVersion:'24.0.0'}),/Unsupported desktop architecture/);
 assert.throws(()=>assertSupportedEnvironment({platform:'linux',arch:'x64',nodeVersion:'22.20.0'}),/requires Node 24\+/);
 assert.throws(()=>assertSupportedEnvironment({platform:'linux',arch:'x64',nodeVersion:'24.0'}),/requires Node 24\+/);
});

test('uses the native runtime and direct executable layout for each OS',()=>{
 assert.deepEqual(desktopLayout('linux'),{
  runtimeFile:'node',
  packagedExecutable:'arch-studio-bin',
  linuxWrapper:'arch-studio',
 });
 assert.deepEqual(desktopLayout('win32'),{
  runtimeFile:'node.exe',
  packagedExecutable:'arch-studio-bin.exe',
  linuxWrapper:null,
 });
 assert.throws(()=>desktopLayout('darwin'),/Unsupported desktop platform/);
});

test('rejects a mismatched install before creating the owned staging tree',async t=>{
 const base=await mkdtemp(path.join(os.tmpdir(),'arch-studio-package-'));
 t.after(()=>rm(base,{recursive:true,force:true}));
 await writeFile(path.join(base,'package.json'),JSON.stringify({name:'arch-studio',version:'0.0.0',devDependencies:{electron:'44.9.1'}}));
 await writeFile(path.join(base,'package-lock.json'),JSON.stringify({packages:{
  '':{devDependencies:{electron:'44.9.1'}},
  'node_modules/electron':{version:'44.9.1'},
 }}));
 await mkdir(path.join(base,'node_modules','electron'),{recursive:true});
 await writeFile(path.join(base,'node_modules','electron','package.json'),JSON.stringify({version:'44.9.0'}));

 await assert.rejects(packageDesktop({base}),/installed Electron is 44\.9\.0/);
 await assert.rejects(access(path.join(base,'.local')),{code:'ENOENT'});
});
