import test from 'node:test';
import assert from 'node:assert/strict';
import {selectWindowsNode} from '../desktop/node-runtime.mjs';
import {hostEnvironment,configurationRelativePath} from '../desktop/arguments.mjs';
import {parseWindowsTools,loadWindowsTools,windowsHostEnvironment} from '../desktop/windows-tools.mjs';

const node=(extra={})=>JSON.stringify({node:'24.21.0',platform:'win32',arch:'x64',...extra});
test('native Open/Save dialog paths retain containment and the existing Host slash protocol',()=>{
 assert.equal(configurationRelativePath('C:\\Project','C:\\Project\\simulation\\Sod\\Sod.par','win32'),'simulation/Sod/Sod.par');
 assert.equal(configurationRelativePath('C:/Project','c:/Project/build/W3-dialog-test.par','win32'),'build/W3-dialog-test.par');
 assert.equal(configurationRelativePath('/project','/project/input.par','linux'),'input.par');
 for(const selected of ['C:/elsewhere/input.par','D:/Project/input.par','C:/Project/source.cpp','C:/Project/../escape.par'])
  assert.throws(()=>configurationRelativePath('C:/Project',selected,'win32'),/inside the managed project/);
});
test('native Windows is the default and explicit Linux projects keep WSL selection',()=>{
 assert.equal(hostEnvironment({cwd:'C:/work',project:'D:/project'},'win32'),'win32');
 assert.equal(hostEnvironment({cwd:'/home/user',project:'D:/project'},'win32'),'win32');
 assert.equal(hostEnvironment({cwd:String.raw`\\wsl.localhost\Ubuntu\home\user`,project:'D:/project'},'win32'),'win32');
 for(const project of ['/home/user/project',String.raw`\\wsl.localhost\Ubuntu\home\user\project`])
  assert.equal(hostEnvironment({cwd:'C:/work',project},'win32'),'wsl');
 assert.equal(hostEnvironment({cwd:'C:/work',distro:'Ubuntu'},'win32'),'wsl');
 assert.equal(hostEnvironment({cwd:'/work'},'linux'),'linux');
 assert.throws(()=>hostEnvironment({distro:'Ubuntu'},'linux'),/Linux launcher/);
});
test('Windows packaged Node precedes PATH and must be native standalone x64 Node',async()=>{
 const seen:string[]=[];
 const selected=await selectWindowsNode({bundled:'C:/package with spaces/runtime/node.exe',searchPath:'C:/tools',probe:async(p:string)=>{seen.push(p);return node();}});
 assert.equal(selected,'C:/package with spaces/runtime/node.exe');assert.equal(seen.length,1);
 for(const extra of [{platform:'linux'},{arch:'arm64'},{node:'22.0.0'},{electron:'44.9.1'}])
  await assert.rejects(selectWindowsNode({explicit:'C:/node.exe',searchPath:'C:/other',probe:async()=>node(extra)}),/ARCH_STUDIO_NODE/);
 await assert.rejects(selectWindowsNode({explicit:'C:node.exe',probe:async()=>node()}),/absolute Windows/);
});
function tools(){return {cmake:'C:/tools/cmake.exe',supervisor:'C:/tools/supervisor.exe',ninja:'C:/tools/ninja.exe',objdump:'C:/tools/objdump.exe',publisher:'C:/tools/publisher.exe',rc:'C:/tools/windres.exe',path:'C:/deps/bin;C:/tools;C:/Windows/System32'};}
test('trusted tool configuration rejects unknown fields, relative executables and inherited PATH components',()=>{
 assert.deepEqual(parseWindowsTools(JSON.stringify(tools())),tools());
 for(const value of [{...tools(),command:'anything'},{...tools(),cmake:'relative.exe'},{...tools(),supervisor:'C:relative.exe'},{...tools(),rc:'C:/tool.cmd'},{...tools(),path:'C:/tools;;C:/Windows'},{...tools(),path:'C:/tools;.'},{...tools(),path:'C:/tools\0'}])
  assert.throws(()=>parseWindowsTools(JSON.stringify(value)));
 assert.throws(()=>parseWindowsTools('not json'),/ARCH_STUDIO_WINDOWS_TOOLS/);
 const env=windowsHostEnvironment(tools(),{SystemRoot:'C:/Windows',ComSpec:'C:/Windows/System32/cmd.exe',PATH:'untrusted',ARCH_STUDIO_WINDOWS_TOOLS:'not inherited',NODE_OPTIONS:'--require bad'});
 assert.deepEqual(env,{PATH:tools().path,PATHEXT:'.COM;.EXE;.BAT;.CMD',SystemRoot:'C:/Windows',ComSpec:'C:/Windows/System32/cmd.exe'});
});
if(process.platform==='win32') test('native trusted tools accept an optional Windows Terminal execution alias',{skip:process.platform!=='win32'||!process.env.ARCH_TEST_TERMINAL||!process.env.ARCH_TEST_PUBLISHER},async()=>{
 const env=process.env;
 const value={cmake:env.ARCH_TEST_CMAKE,supervisor:env.ARCH_TEST_SUPERVISOR,ninja:env.ARCH_TEST_NINJA,objdump:env.ARCH_TEST_OBJDUMP,publisher:env.ARCH_TEST_PUBLISHER,rc:env.ARCH_TEST_RC,path:env.PATH,terminal:env.ARCH_TEST_TERMINAL};
 assert.deepEqual(await loadWindowsTools(JSON.stringify(value)),value);
});
