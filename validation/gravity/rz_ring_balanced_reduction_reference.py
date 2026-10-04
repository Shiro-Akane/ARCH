#!/usr/bin/env python3
"""Independent exact sum containment and old-scan choice for actual workspace.
Raw updates stay local, output contains scalar counters and identities.
"""
import argparse,hashlib,json,subprocess
from pathlib import Path
from fractions import Fraction as F
def main():
 p=argparse.ArgumentParser();p.add_argument('--probe',type=Path,required=True)
 p.add_argument('--output-root',type=Path,required=True);a=p.parse_args()
 out=a.output_root.resolve()
 if out.exists():p.error('output-root must be new')
 out.mkdir(parents=True)
 q=subprocess.run([str(a.probe.resolve()),'ring-balanced-reduction-probe'],capture_output=True,text=True)
 (out/'probe.json').write_text(q.stdout);(out/'stderr.log').write_text(q.stderr)
 if q.returncode:raise SystemExit(q.returncode)
 rows=[]
 for c in json.loads(q.stdout)['cases']:
  leaves={};depth=(c['capacity']-1).bit_length()
  for i,step in enumerate(c['steps']):
   leaves[step['index']]=(step['lower'],step['upper'])
   lo=sum((F(x[0]) for x in leaves.values()),F(0))
   hi=sum((F(x[1]) for x in leaves.values()),F(0))
   assert F(step['total_lower'])<=lo<=hi<=F(step['total_upper'])
   # Original choice uses FP64 width; it is a subdivision heuristic, not proof.
   worst=min(leaves,key=lambda k:(-(leaves[k][1]-leaves[k][0]),k))
   assert step['worst']==worst
   assert step['updates']==(i+1)*(depth+1)
  rows.append({'capacity':c['capacity'],'updatesChecked':len(c['steps']),
   'nodeUpdates':c['steps'][-1]['updates'],'pathDepth':depth,'maximumActiveLeaves':len(leaves)})
 result={'status':'PASS','cases':len(rows),'replacements':sum(r['updatesChecked'] for r in rows),
 'rows':rows,'probeSha256':hashlib.sha256(a.probe.read_bytes()).hexdigest(),
 'scope':'Exact stored leaf interval sums and unchanged largest-width earliest-index choice; bounded logarithmic update counts',
 'limitations':['No physical ring integral or full RZ release certificate from this workspace check','Raw workspace arrays retained locally']}
 (out/'summary.json').write_text(json.dumps(result,indent=2)+'\n')
 print('RING_BALANCED_REDUCTION_FRACTION_PASS',len(rows),result['replacements'])
if __name__=='__main__':main()
