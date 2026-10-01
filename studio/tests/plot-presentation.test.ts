import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {projection,zoomView,panView,axisDefault,fieldDefault,displayRange,fieldRange,logDataError,previewCoordinateDomain} from '../src/data/plotPresentation.ts';
import {realInitGrid,gridPoint,sampleEdges} from '../src/data/RealInitPreviewProvider.ts';
const near=(a:number,b:number)=>assert.ok(Math.abs(a-b)<Math.max(1,Math.abs(b))*1e-10,`${a} != ${b}`);
test('all four 1D scale combinations preserve data/marker/hit inverse under zoom and pan',()=>{
 for(const xScale of ['linear','log'] as const)for(const yScale of ['linear','log'] as const){const xv=panView(zoomView([0,1],.6,.4),.1),yv=panView(zoomView([0,1],.4,.5),-.1);const xp=projection([.001,1],xScale,xv),yp=projection([.1,2],yScale,yv);for(const x of [.01,.125,.5,.9])near(xp.inverse(xp.forward(x)),x);for(const raw of [.125,1])near(yp.inverse(yp.forward(raw)),raw);near(projection([0,1],'linear').forward(.5),.5);}
});
test('non-square Core grids in both directions retain j*Nx+i through independent spatial scales',()=>{
 for(const name of ['cellular-x1','cellular-x2']){const core=JSON.parse(readFileSync(new URL(`../../src/api/examples/core-b/${name}.json`,import.meta.url),'utf8'));const grid=realInitGrid({core} as never,'DENS'),original=Array.from(grid.values);assert.notEqual(grid.width,grid.height);
 for(const xs of ['linear','log'] as const)for(const ys of ['linear','log'] as const){const xp=projection([grid.x[0]/2,30],xs,zoomView([0,1],.5,.6)),yp=projection([grid.y[0]/2,15],ys,panView(zoomView([0,1],.5,.7),.1));for(let j=0;j<grid.height;j++)for(let i=0;i<grid.width;i++){const hit=gridPoint(grid,xp.inverse(xp.forward(grid.x[i])),yp.inverse(yp.forward(grid.y[j])));assert.equal(hit?.index,j*grid.width+i);}}
 assert.deepEqual(Array.from(grid.values),original);const edges=sampleEdges(grid.x);assert.equal(gridPoint(grid,edges[0]-1,grid.y[0]),null);}
});
test('Log rejects nonpositive raw data independently of manual range/clipping',()=>{assert.match(logDataError([1,0,-1],'log','Field')!,/sample 1/);assert.match(logDataError([-2,3],'log','X')!,/-2/);assert.equal(logDataError([0,-1],'linear','Field'),null);assert.throws(()=>displayRange({...axisDefault(),scale:'log'},[0,1]),/positive/);});
test('physical manual ranges and independent clipping reject invalid drafts without changing input',()=>{
 const raw=[.125,1];const before=[...raw];assert.deepEqual(fieldRange({...fieldDefault(),lower:true,low:'.2'},[.125,1]),[.2,1]);assert.deepEqual(fieldRange({...fieldDefault(),upper:true,high:'.8'},[.125,1]),[.125,.8]);assert.deepEqual(fieldRange({...fieldDefault(),lower:true,low:'.2',upper:true,high:'.8'},[.125,1]),[.2,.8]);for(const min of ['','NaN','1e999','2','3abc'])assert.throws(()=>displayRange({...axisDefault(),manual:true,min,max:'1'},[0,1]));assert.throws(()=>fieldRange({...fieldDefault(),lower:true,low:'2'},[0,1]));assert.deepEqual(raw,before);
});
test('zoom bounds remain finite and Fit is exact full-domain projection',()=>{let v:[number,number]=[0,1];for(let i=0;i<200;i++)v=zoomView(v,.99,.8);assert.ok(v[1]>v[0]);v=panView(v,1e6);assert.ok(v[0]>=0&&v[1]<=1);const full=projection([0,25.6],'linear');assert.equal(full.inverse(0),0);assert.equal(full.inverse(1),25.6);});

test('authoritative Core region restores exact full domain; unrepresentable display ranges reject',()=>{assert.deepEqual(previewCoordinateDomain({grid:{axes:[{name:'x1',min:0,max:25.6}]}},'x1'),[0,25.6]);assert.equal(previewCoordinateDomain({grid:{axes:[{name:'x1',min:0,max:Infinity}]}},'x1'),undefined);assert.throws(()=>projection([-1e308,1e308],'linear'),/represented/);});
