/** Reproduce exact stored-boundary findings through the production isolated reader. */
import {readFileSync} from 'node:fs';
import {createHash} from 'node:crypto';
import assert from 'node:assert/strict';
import {readPlotfilePointIsolated} from '../../studio/host/isolatedPlotfileMetadata.ts';
const [input]=process.argv.slice(2);assert.equal(process.argv.length,3);
const {plot,queries}=JSON.parse(readFileSync(input,'utf8'));
const sha=()=>createHash('sha256').update(readFileSync(plot)).digest('hex');
const before=sha(),results=[];
for(const q of queries){
 assert(['gap','overlap'].includes(q.kind));
 assert.equal(q.matchCount,q.kind==='gap'?0:2);
 let message=null;
 try{await readPlotfilePointIsolated(plot,{field:'DENS',point:q.point});}
 catch(error){message=error instanceof Error?error.message:String(error);}
 assert(message?.includes(q.kind==='gap'?'NO_NATIVE_CELL':'AMBIGUOUS_NATIVE_CELL'));
 results.push({...q,error:message});
}
assert.equal(sha(),before);
console.log(JSON.stringify({plotfileSha256:before,sourceUnchanged:true,results,
 scope:'Real stored-boundary Inspector failure reproduction; no tolerance acceptance or geometry repair'},null,2));
