import test from 'node:test';
import assert from 'node:assert/strict';
import { enumControls, rawOption } from '../src/components/ParameterPanel/controlContract.ts';
import { loadPar, exportPar } from '../src/state/parState.ts';
test('UI enum retains unknown and mixed-case raw tokens without document normalization',()=>{
 for(const value of ['HLLC','future_solver']) {
  assert.equal(rawOption(value,enumControls.solver)?.value,value);
  const raw=`solver = ${value} # keep\r\ncustom = auto\r\n`;
  assert.equal(exportPar(loadPar('case.par',raw)).text,raw);
 }
 assert.equal(enumControls.eos_type.includes('tabular3d'),false);
 assert.equal(enumControls.eos_type.includes('tabular'),true);
 assert.equal(enumControls.network_name,undefined);
});
