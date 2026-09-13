import { test } from 'node:test';
import assert from 'node:assert/strict';
import { loadPar, parErrors, parStatus } from '../src/state/parState.ts';
test('known types, enums, required conditional fields and explicit constraints',()=>{
 for (const raw of ['nblockx1=1.2','cfl=NaN','tmax=Infinity','restart=1','use_nse=guess','refine_threshold=0.1\nderefine_threshold=0.2','restart=true','regrid_interval=0']) assert.equal(parStatus(loadPar('bad.par',raw)),'invalid',raw);
 assert.equal(Object.keys(parErrors(loadPar('good.par','restart=FALSE\nuse_nse=auto\ntmax=1.5e-8\ncfl=0.4'))).length,0);
});
test('unknown metadata does not invent range or scientific semantics',()=>{
 assert.equal(parStatus(loadPar('custom.par','temperature=0.5\nfuture=arbitrary literal\nxhe4=2.5')),'saved');
});

test('prototype-like unknown keys remain plain user parameters',()=>{
 const s=loadPar('custom.par','constructor=text\n__proto__=0.5\ntoString=unknown');
 assert.equal(parStatus(s),'saved');assert.equal(Object.keys(parErrors(s)).length,0);
});

test('coordinate expressions follow confirmed finite restricted pi grammar',()=>{
 for(const value of ['2*pi','pi/2','-pi','1e-3']) assert.equal(parStatus(loadPar('math.par',`x2_max=${value}`)),'saved');
 for(const value of ['NaN','Infinity','pi/0','sin(pi)','abc']) assert.equal(parStatus(loadPar('math.par',`x2_max=${value}`)),'invalid');
});
