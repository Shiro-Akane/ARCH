/** Keep the current Cartesian reader from treating internal RZ as supported. */
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import crypto from 'node:crypto';
import {inspectPlotfileMetadataIsolated} from '../../studio/host/isolatedPlotfileMetadata.ts';
assert.equal(process.argv.length,4,'Use fixture evidence root and new summary JSON');
const root=process.argv[2];
const hash=async p=>crypto.createHash('sha256').update(await fs.readFile(p)).digest('hex');
const find=async mode=>root+'/'+mode+'/'+(await fs.readdir(root+'/'+mode)).find(n=>n.includes('_plt_')&&n.endsWith('.h5'));
const cart=await find('cartesian'),rz=await find('rz');
const cartBefore=await hash(cart),rzBefore=await hash(rz);
const m=await inspectPlotfileMetadataIsolated(cart);
assert.equal(m.geometry,'cartesian');
assert.equal(m.candidateNativeGrid.version,'candidate-cartesian-1');
let rejected;
try {await inspectPlotfileMetadataIsolated(rz);}
catch(error){rejected=error;}
assert.equal(rejected?.code,'WORKER_FAILED');
assert.match(rejected.message,/Unsupported candidate native geometry/);
assert.equal(await hash(cart),cartBefore);assert.equal(await hash(rz),rzBefore);
const summary={status:'PASS',cartesianMetadata:'accepted',internalRzMetadata:'explicitly rejected',
 errorCode:rejected.code,diagnostic:rejected.message,fileHashesUnchanged:true,
 scope:'Current production isolated reader capability gate; not RZ Viewer or science acceptance'};
await fs.writeFile(process.argv[3],JSON.stringify(summary,null,2)+'\n',{flag:'wx'});
console.log(JSON.stringify(summary,null,2));
