import process from 'node:process';
import console from 'node:console';
import {File,ready,Dataset} from 'h5wasm/node';
import {writeFile} from 'node:fs/promises';
await ready;
const [sourceFinal,resumedFinal,output]=process.argv.slice(2);
if(!sourceFinal||!resumedFinal||!output)throw Error('Usage: node tests/tools/compareRestartCheckpoints.mjs source-final.h5 resumed-final.h5 summary.json');
const paths=[sourceFinal,resumedFinal];
const files=paths.map(p=>new File(p,'r'));
const attributes=o=>Object.fromEntries(Object.entries(o.attrs).map(([k,v])=>[k,v.json_value]));
const pathSets=files.map(f=>f.paths().sort());
if(JSON.stringify(pathSets[0])!==JSON.stringify(pathSets[1]))throw Error('Checkpoint object paths differ');
const comparisons=[];
for(const p of ['/',...pathSets[0]]){
 const [a,b]=p==='/'?files:files.map(f=>f.get(p));
 if(JSON.stringify(attributes(a))!==JSON.stringify(attributes(b)))throw Error('Attributes differ: '+p);
 if(a instanceof Dataset){
  if(!(b instanceof Dataset)||JSON.stringify(a.shape)!==JSON.stringify(b.shape)||JSON.stringify(a.dtype)!==JSON.stringify(b.dtype))throw Error('Dataset layout differs: '+p);
  const x=a.value,y=b.value;
  if(ArrayBuffer.isView(x)&&ArrayBuffer.isView(y)){
   if(x.length!==y.length)throw Error('Dataset length differs: '+p);
   let differences=0,maxAbs=0;
   for(let i=0;i<x.length;i++){if(x[i]!==y[i])differences++;if(typeof x[i]==='number'){if(!Number.isFinite(x[i])||!Number.isFinite(y[i]))throw Error('Nonfinite value: '+p);maxAbs=Math.max(maxAbs,Math.abs(x[i]-y[i]));}}
   comparisons.push({path:p,shape:a.shape,count:x.length,differences,maxAbs});
   if(differences)throw Error('Dataset values differ: '+p);
  }else{
   if(JSON.stringify(x)!==JSON.stringify(y))throw Error('Dataset values differ: '+p);
   comparisons.push({path:p,shape:a.shape,equal:true});
  }
 }
}
const metadata=files.map(attributes);
if(!Number.isFinite(metadata[0].time)||!Number.isInteger(metadata[0].step))throw Error('Missing physical endpoint');
files.forEach(f=>f.close());
const result={scope:'Exact final-checkpoint equality; not independent scientific accuracy or desktop UAT',status:'PASS',metadata,objectCount:pathSets[0].length,comparedDatasetCount:comparisons.length,comparisons};
await writeFile(output,JSON.stringify(result,null,2));
console.log(JSON.stringify({status:result.status,objectCount:result.objectCount,comparedDatasetCount:result.comparedDatasetCount,metadata,comparisons}));
