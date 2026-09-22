import test from 'node:test';import assert from 'node:assert/strict';
import {registeredSourceCase} from '../host/desktopSource.ts';
test('desktop source association accepts real absolute and relative registry paths',()=>{
 const absolute=[{caseId:'Sod',inspection:{sourceFile:'/project/simulation/Sod/Sod.cpp'}}];
 assert.equal(registeredSourceCase(absolute,'/project','simulation/Sod/Sod.cpp'),'Sod');
 assert.equal(registeredSourceCase([{caseId:'Sod',inspection:{sourceFile:'simulation/Sod/Sod.cpp'}}],'/project','/project/simulation/Sod/Sod.cpp','Sod'),'Sod');
 assert.throws(()=>registeredSourceCase(absolute,'/project','other/Sod.cpp'));
 assert.throws(()=>registeredSourceCase(absolute,'/project','../other/Sod.cpp'));
 assert.throws(()=>registeredSourceCase(absolute,'/project','simulation/Sod/Sod.cpp','CellularDet'));
 assert.throws(()=>registeredSourceCase([{caseId:'Sod',inspection:{sourceFile:null}}],'/project','simulation/Sod/Sod.cpp'));
 assert.throws(()=>registeredSourceCase([...absolute,...absolute],'/project','simulation/Sod/Sod.cpp'));
});
