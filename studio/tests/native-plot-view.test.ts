import {test} from 'node:test';
import assert from 'node:assert/strict';
import {finitePlotRange,nativePlotDomain,panPlotView,pickNativePlotCell,plotFraction,plotValue,zoomPlotView} from '../src/data/nativePlotView.ts';
import type {NativePlotCells} from '../src/host/plotfileAudit.ts';
function native():NativePlotCells{
 const x=[0,1,2,0,1,2],y=[10,10,10,11,11,11];
 return {version:'candidate-cartesian-1',measureSource:'GridMetrics::CellVolume',
 measureConvention:'active-coordinate-product; inactive-measures-omitted',measureUnit:null,
 identityScope:'file-local',logicalKey:'1/0/0/0',level:1,logicalCoordinates:[0,0,0],
 lower:{x1:x,x2:y,x3:x.map(()=>0)},upper:{x1:x.map(v=>v+1),x2:y.map(v=>v+1),x3:x.map(()=>0)},
 cellMeasure:x.map(()=>1)};
}
test('non-square x1-fastest native geometry maps all cells before and after zoom/pan',()=>{
 const n=native(),original=JSON.stringify(n),view=nativePlotDomain(n,2,[0,1,2,3,4,5])!;
 assert.deepEqual(view,{x:[0,3],y:[10,12]});
 const changed=panPlotView(zoomPlotView(view,.5,.2,.7),-.4,.25);
 for(let i=0;i<6;i++){
  const x=n.lower.x1[i]+.5,y=n.lower.x2[i]+.5;
  const fx=plotFraction(x,changed.x),fy=plotFraction(y,changed.y);
  assert.equal(pickNativePlotCell(n,2,plotValue(fx,changed.x),plotValue(fy,changed.y)),i);
 }
 assert.equal(JSON.stringify(n),original);
 assert.equal(pickNativePlotCell(n,2,-1,10.5),null);
 assert.equal(pickNativePlotCell(n,2,1.5,13),null);
});
test('zoom anchors remain fixed; pan and inverse restore the view; invalid collapse is rejected',()=>{
 const view={x:[0,10] as [number,number],y:[-4,6] as [number,number]};
 const zoom=zoomPlotView(view,.3,.2,.8);
 assert.ok(Math.abs(plotValue(.2,zoom.x)-plotValue(.2,view.x))<1e-14);
 assert.ok(Math.abs(plotValue(.8,zoom.y)-plotValue(.8,view.y))<1e-14);
 assert.deepEqual(panPlotView(panPlotView(view,.2,-.3),-.2,.3),view);
 assert.equal(zoomPlotView(view,0),view);
 assert.equal(zoomPlotView(view,NaN),view);
 assert.equal(zoomPlotView(view,Number.MIN_VALUE),view);
 assert.equal(panPlotView(view,Infinity,0),view);
});
test('1D uses stored x bounds and raw field range; nonfinite values remain unmodified',()=>{
 const n=native(),values=[1,'NaN',.125,'Infinity',1,1] as const;
 assert.deepEqual(nativePlotDomain(n,1,[...values]),{x:[0,3],y:[.125,1]});
 assert.deepEqual(finitePlotRange(['NaN','Infinity','-Infinity']),null);
 assert.deepEqual(finitePlotRange([0,0]),[-.5,.5]);
 assert.deepEqual(values,[1,'NaN',.125,'Infinity',1,1]);
 assert.equal(pickNativePlotCell(n,1,2.5,1e20),2);
 assert.equal(nativePlotDomain(n,3,[1]),null);
});
