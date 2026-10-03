import {PlotfileSourceEvidence} from './PlotfileSourceEvidence';
import {storedCellIndices} from '../host/plotfileAudit';
import type {AuditResponse} from '../host/plotfileAudit';

export function PlotfileNativeInspector({samples,row}:{samples:AuditResponse;row:number}){
 const a=samples.audit,p=a.payload;
 if(!p||row<0||row>=p.values.length)return null;
 const n=p.nativeCells;
 return <section aria-label="Plotfile sample Inspector">
  <h3>{n?'Native stored cell Inspector · candidate':'Stored sample Inspector'}</h3>
  <dl>
   <dt>Observed file</dt><dd>{samples.relativePath}</dd>
   <dt>File SHA-256</dt><dd className="audit-digest">{a.file.sha256}</dd>
   <dt>Field / raw value</dt><dd>{p.field} · {String(p.values[row])} · unit unknown</dd>
   <dt>Time</dt><dd>{a.time}</dd>
   <dt>Stored block / global index</dt><dd>{p.block} / {p.linearIndices[row]}</dd>
   <dt>Local i / j / k · no ghost</dt><dd>{storedCellIndices(a,p.block,p.linearIndices[row]).join(' / ')}</dd>
   <dt>Stored Cartesian center</dt><dd>{['x','y','z'].map(key=>String(p.coordinates[key as 'x'|'y'|'z'][row])).join(' / ')}</dd>
   {n&&<>
    <dt>File-local logical key / level</dt><dd>{n.logicalKey} / {n.level}</dd>
    {(['x1','x2','x3'] as const).map((axis,i)=><div key={axis}>
     <dt>{axis} bounds · {i<a.dimension?'active':'inactive'}</dt>
     <dd>[{String(n.lower[axis][row])}, {String(n.upper[axis][row])}]</dd>
    </div>)}
    <dt>Stored cell measure · unit unknown</dt><dd>{String(n.cellMeasure[row])}</dd>
    <dt>Measure source / convention</dt><dd>{n.measureSource} · {n.measureConvention}</dd>
   </>}
  </dl>
  <PlotfileSourceEvidence evidence={a.candidateSourceIdentity}/>
  <p>{n?'Candidate native metadata; scientific review and units remain pending. Logical identity is scoped to this file digest.':'Native bounds and cell measure were not recorded in this file.'} Values are read from storage, without interpolation or LOD aggregation.</p>
 </section>;
}
