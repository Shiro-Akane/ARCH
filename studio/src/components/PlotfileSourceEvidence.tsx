import type {PlotfileSourceEvidence as Evidence} from '../host/plotfileSourceIdentity';
export function PlotfileSourceEvidence({evidence}:{evidence?:Evidence|null}){
 if(!evidence)return <p>Source identity was not recorded; no association is inferred from this project.</p>;
 return <section aria-label="Recorded Plotfile source evidence">
  <h3>Recorded source evidence · partial candidate</h3>
  <dl>
   <dt>Case / source</dt><dd>{evidence.caseId??'unknown'} · {evidence.caseSource??'unknown'}</dd>
   <dt>Loaded raw config SHA-256</dt><dd className="audit-digest">{evidence.rawConfigSha256??'unknown'}</dd>
   <dt>Raw config source</dt><dd>{evidence.rawConfigSource??'unknown'}</dd>
   <dt>Running binary SHA-256</dt><dd className="audit-digest">{evidence.binarySha256??'unknown'}</dd>
   <dt>Binary source / scope</dt><dd>{evidence.binarySource??'unknown'} · {evidence.binaryScope}</dd>
   <dt>EOS / source</dt><dd>{evidence.eosType??'unknown'} · {evidence.eosSource??'unknown'}</dd>
   <dt>EOS table</dt><dd>{evidence.eosTableState} · {evidence.eosTableSha256??'no recorded digest'}</dd>
   <dt>IdealGas gamma</dt><dd>{evidence.idealGamma??'not recorded'}</dd>
   <dt>Ordered species</dt><dd>{evidence.speciesState} · {evidence.speciesNames.join(', ')||'none recorded'}</dd>
   <dt>Run / effective config / build / source Git / unit system</dt><dd>unknown</dd>
  </dl>
  <p>Recorded candidate evidence does not certify full scientific provenance or freshness. Raw config differs from effective config; the binary digest covers the main executable only.</p>
 </section>;
}
