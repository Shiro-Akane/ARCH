import test from 'node:test';
import assert from 'node:assert/strict';
import {parseLaunchArgs,windowsAssociation} from '../desktop/arguments.mjs';

test('desktop launch preserves semantic arguments and spaced paths',()=>{
 assert.deepEqual(parseLaunchArgs(['--case','Sod','--config','test inputs/Sod.par'],'/project'),{cwd:'/project',case:'Sod',config:'test inputs/Sod.par'});
 assert.deepEqual(parseLaunchArgs(['--project','/project one','--binary','build/bin/ARCH'],'/else'),{cwd:'/else',project:'/project one',binary:'build/bin/ARCH'});
 assert.equal(parseLaunchArgs(['--source','/project/Sod.cpp','--cwd','/project/nested'],'/else').cwd,'/project/nested');
 for(const args of [['--command','id'],['--case'],['--case','Sod','--case','Other'],['--cwd','/a','--cwd','/b'],['--config','--project']])assert.throws(()=>parseLaunchArgs(args,'/project'));
});

test('Windows/WSL mapping keeps distro identity and literal spaces',()=>{
 const unc=String.raw`\\wsl.localhost\ARCH-Ubuntu-24.04\home\arch\project with spaces`;
 assert.deepEqual(windowsAssociation(unc),{distro:'ARCH-Ubuntu-24.04',linux:'/home/arch/project with spaces'});
 assert.throws(()=>windowsAssociation(unc,'another-distro'),/different WSL/);
 assert.deepEqual(windowsAssociation('/home/arch/a b','Linux'),{distro:'Linux',linux:'/home/arch/a b'});
 assert.equal(windowsAssociation('E:\\release with spaces\\app').windows,'E:\\release with spaces\\app');
 assert.throws(()=>windowsAssociation('bad\0path'));
});
