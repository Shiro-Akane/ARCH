import type {PlotfileSourceEvidence as Evidence} from '../host/plotfileSourceIdentity';
export function PlotfileSourceEvidence({evidence}:{evidence?:Evidence|null}){
 if(!evidence)return <p>Source identity was not recorded; no association is inferred from this project.</p>;
 return <section aria-label="Recorded Plotfile source evidence">
  <h3>Recorded source evidence · {evidence.version==='arch-plot-identity-1'?'resolved runtime':'partial candidate'}</h3>
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
   <dt>Run output session</dt><dd>{evidence.runId??'unknown'} · {evidence.runIdSource??'unknown'}</dd>
   {(['effectiveConfigSha256','buildId','sourceGitHead'] as const).map((key,index)=>
    <div key={key}><dt>{['Effective config','Build Manifest','Source Git'][index]}</dt>
     <dd>{evidence[key]??'unknown'}{evidence[key]===null?' · '+(evidence.unknownIdentityReasons?.[key]??'not recorded in this file'):''}</dd></div>)}
   {evidence.version==='arch-plot-identity-1'&&<>
    <dt>Compiled case SHA-256</dt><dd className="audit-digest">{evidence.caseSourceSha256}</dd>
    <dt>Project-source manifest SHA-256</dt><dd className="audit-digest">{evidence.sourceManifestSha256}</dd>
    <dt>Compiler-profile SHA-256 / scope</dt><dd className="audit-digest">{evidence.buildProfileSha256} · {evidence.buildIdentityScope}</dd>
    <dt>EOS model SHA-256</dt><dd className="audit-digest">{evidence.eosIdentitySha256}</dd>
    <dt>Actual compute backend</dt><dd>{evidence.resolvedBackend}</dd>
   </>}
   <dt>Recorded unit system</dt><dd>{evidence.eosUnitSystem??'unknown'}</dd>
  </dl>
  {evidence.speciesProperties?.state==='recorded'?<details>
   <summary>Recorded EOS constituents · {evidence.speciesNames.length} species</summary>
   <p>Resolved runtime constituent values in recorded species order. Values are displayed exactly as recorded, without conversion.</p>
   <table><thead><tr><th>Species</th><th>A</th><th>Z</th><th>gamma</th><th>Cv · raw</th></tr></thead>
    <tbody>{evidence.speciesNames.map((name,index)=><tr key={index}>
     <th>{name}</th>{(['A','Z','gamma','Cv'] as const).map(key=><td key={key}>
      {evidence.speciesProperties?.state==='recorded'?String(evidence.speciesProperties.values[key][index]):''}
     </td>)}
    </tr>)}</tbody></table>
   <p>Source: {evidence.speciesProperties.source}</p>
  </details>:<p>EOS constituent properties: {evidence.speciesProperties?.state==='unknown'?
   evidence.speciesProperties.reason:'not recorded in this file'}</p>}
  <p>Configuration and build records identify the producing run. Raw config differs from effective config; the binary digest covers the main executable and the build record covers project sources and the configured compiler profile. Numerical accuracy and runtime shared-library closure are separate qualifications.</p>
 </section>;
}
