import test from 'node:test';
import assert from 'node:assert/strict';
import { modeDescriptions } from '../src/data/modePresentation.ts';
test('mode requirements distinguish real file inputs from built-in demos',()=>{
 assert.match(modeDescriptions.config,/local .par/); assert.match(modeDescriptions.plotfile,/local .h5/);
 assert.match(modeDescriptions.mock,/No ARCH file/); assert.match(modeDescriptions.cellular,/Read-only.*No local file/);
});
