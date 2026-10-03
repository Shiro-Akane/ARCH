import {formatPlotfileRawValue} from '../host/plotfileRawValue';
import {PlotfileSourceEvidence} from './PlotfileSourceEvidence';
import {storedCellIndices} from '../host/plotfileAudit';
import type {AuditResponse} from '../host/plotfileAudit';

export function PlotfileNativeInspector({samples,row}:{samples:AuditResponse;row:number}){
 const a=samples.audit,p=a.payload;
 if(!p||row<0||row>=p.values.length)return null;
 const n=p.nativeCells,d=a.fields.find(f=>f.name===p.field)?.declaration;
 return <section aria-label="Plotfile sample Inspector">
  <h3>{n?'Native stored cell Inspector · candidate':'Stored sample Inspector'}</h3>
  <dl>
   <dt>Observed file</dt><dd>{samples.relativePath}</dd>
   <dt>File SHA-256</dt><dd className="audit-digest">{a.file.sha256}</dd>
   <dt>Field / raw value</dt><dd>{p.field} · {formatPlotfileRawValue(p.values[row])} · {p.unit??'unit unknown'}</dd>
   <dt>Recorded field meaning / basis</dt><dd>{d?.meaning??'unknown'} / {d?.basis??'unknown'}</dd>
   {d?.unitReason&&<><dt>Unknown unit reason</dt><dd>{d.unitReason}</dd></>}
   {a.pointEvidence&&<><dt>Queried physical point</dt><dd>{a.pointEvidence.point.join(' / ')} · {a.pointEvidence.rule}</dd>
    <dt>Point search coverage</dt><dd>{a.pointEvidence.scannedCells} stored cells scanned · one exact match · no interpolation</dd></>}
   <dt>Time</dt><dd>{a.time} · {a.timeUnit??'unit unknown'}</dd>
   <dt>Stored block / global index</dt><dd>{p.block} / {p.linearIndices[row]}</dd>
   <dt>Local i / j / k · no ghost</dt><dd>{storedCellIndices(a,p.block,p.linearIndices[row]).join(' / ')}</dd>
   <dt>Stored Cartesian center · {a.coordinates.units??'unit unknown'}</dt><dd>{['x','y','z'].map(key=>formatPlotfileRawValue(p.coordinates[key as 'x'|'y'|'z'][row])).join(' / ')}</dd>
   {n&&<>
    <dt>File-local logical key / level</dt><dd>{n.logicalKey} / {n.level}</dd>
    {(['x1','x2','x3'] as const).map((axis,i)=><div key={axis}>
     <dt>{axis} bounds · {i<a.dimension?'active':'inactive'}</dt>
     <dd>[{formatPlotfileRawValue(n.lower[axis][row])}, {formatPlotfileRawValue(n.upper[axis][row])}]</dd>
    </div>)}
    <dt>Stored cell measure · {n.measureUnit??'unit unknown'}</dt><dd>{formatPlotfileRawValue(n.cellMeasure[row])}</dd>
    <dt>Measure normalization</dt><dd>{n.measureNormalization??'unknown'}</dd>
    <dt>Measure source / convention</dt><dd>{n.measureSource} · {n.measureConvention}</dd>
   </>}
  </dl>
  <PlotfileSourceEvidence evidence={a.candidateSourceIdentity}/>
  <p>{n?'Candidate native metadata; scientific review remains pending; recorded units do not certify provenance. Logical identity is scoped to this file digest.':'Native bounds and cell measure were not recorded in this file.'} Values are read from storage, without interpolation or LOD aggregation.</p>
 </section>;
}
