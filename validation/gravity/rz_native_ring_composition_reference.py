#!/usr/bin/env python3
"""Exact Fraction ideal RMS and T_safe for actual unsolved ring source arrays.
Checks reduction/original-request semantics; not a solved physical field proof.
"""
import argparse,hashlib,json
from pathlib import Path
from fractions import Fraction as F
def main():
 p=argparse.ArgumentParser();p.add_argument('--probe-record',type=Path,required=True)
 p.add_argument('--output',type=Path,required=True);a=p.parse_args()
 if a.output.exists():p.error('output must be new')
 rows=[];uncertified=0
 for c in json.loads(a.probe_record.read_text())['cases']:
  if not c['root_exact']:
   assert 'native_total_upper' not in c
   uncertified+=1;continue
  vols=[]
  for cell in c['source_geometry']:
   dr=F(c['root_spacing'][0])/2**cell['level'];dz=F(c['root_spacing'][1])/2**cell['level']
   rl=F(c['root_origin'][0])+cell['index'][0]*dr
   vols.append(((rl+dr)**2-rl**2)*dz)
  weights=[v/sum(vols) for v in vols]
  norm2=lambda xs:sum((w*F(x)**2 for w,x in zip(weights,xs)),F(0))
  rhs2=norm2(c['rhs']);error2=norm2(c['native_rhs_error_cells'])
  E=F(c['native_rhs_error_upper']);tol=F(c['native_tolerance_safe'])
  assert error2<=E**2 and F(c['native_rhs_norm_lower'])**2<=rhs2
  assert F(c['native_residual_upper'])**2>=rhs2
  assert F(c['native_total_upper'])>=F(c['native_residual_upper'])+E
  if tol>0:assert (tol/F(1e-10)+E)**2<=rhs2
  if c['zero']:assert E==tol==F(c['native_total_upper'])==0
  else:assert F(c['native_total_upper'])>tol
  rows.append({'radialOrigin':c['origin'],'mixed':bool(c['mixed']),'zero':bool(c['zero']),
   'cells':len(vols),'totalResidualUpper':c['native_total_upper'],'toleranceSafe':float(tol),
   'rhsErrorUpper':float(E)})
 summary={'status':'PASS','cases':len(rows),'uncertifiedCoordinateCases':uncertified,
  'cells':sum(r['cells'] for r in rows),'rows':rows,
  'probeRecordSha256':hashlib.sha256(a.probe_record.read_bytes()).hexdigest(),
  'scope':'Exact ideal native RMS and original T_safe reductions from current unsolved actual source/RHS arrays',
  'limitations':['Nonzero arrays unsolved and rejected; zero exact accepted',
   'Per-cell physical source/B/A error construction covered by independent companion references',
   'Not a physical solved Phi/force certificate']}
 a.output.parent.mkdir(parents=True,exist_ok=True)
 a.output.write_text(json.dumps(summary,indent=2)+'\n')
 print('NATIVE_NORM_ORIGINAL_REQUEST_FRACTION_PASS',len(rows),summary['cells'])
if __name__=='__main__':main()
