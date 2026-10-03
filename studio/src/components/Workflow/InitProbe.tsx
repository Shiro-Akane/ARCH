import type {CaseProbe} from '../../host/workflowContracts';
import {inspectionSpecies} from '../../host/workflowValidation';

/** Raw, sparse Init outputs remain distinct from field samples and AMR cells. */
export function InitProbe({data,state}:{data:CaseProbe;state:unknown}){
 const species=inspectionSpecies(state);
 return <details><summary>Raw Init probes · {data.sampleCount} samples</summary>
  <p>{data.valueLocation}. {data.sampling}. Velocity: {data.velocityBasis}.</p>
  <p>Composition is raw Init output in Core species order; no normalization or EOS conversion is applied here. These probes are not a full composition field.</p>
  {data.samples.map((s,i)=><div key={i}>
   <h5>Probe {i} · ({s.cartesianPosition.join(', ')}) {s.positionUnit}</h5>
   <p>Thermodynamic input: {s.thermodynamicInput}</p>
   <dl>{s.fields.map(f=><div key={f.key}><dt>{f.key} · {f.consumedByConversion?'consumed input':'unused by conversion'}</dt><dd>{f.value} {f.unit??'Unit unknown'}</dd></div>)}</dl>
   {species.length?<table aria-label={'Probe '+i+' mass fractions'}><thead><tr><th>Species index</th><th>Species</th><th>Raw mass fraction</th><th>Unit</th></tr></thead>
    <tbody>{species.map(p=><tr key={p.index}><td>{p.index}</td><td>{p.name}</td><td>{Object.is(s.massFractions[p.index],-0)?'-0':String(s.massFractions[p.index])}</td><td>{s.massFractionUnit}</td></tr>)}</tbody>
   </table>:<p>No species registered by Core; no composition is inferred.</p>}
  </div>)}
 </details>;
}
