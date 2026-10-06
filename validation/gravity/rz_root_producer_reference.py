#!/usr/bin/env python3
"""Exact Fraction root source/observer identity from actual ring producer.
Consumes an existing probe record, never reruns integration or imports production
coordinate/proof helpers. Raw source/observer arrays remain local.
"""
import argparse,hashlib,json
from fractions import Fraction as F
from pathlib import Path
def main():
    p=argparse.ArgumentParser()
    p.add_argument('--probe-record',required=True,type=Path)
    p.add_argument('--output',required=True,type=Path)
    a=p.parse_args()
    if a.output.exists():p.error('output must be new')
    rows=[];ncell=nface=0;certified=declined=0
    for c in json.loads(a.probe_record.read_text())['cases']:
        origins=list(map(F,c['root_origin']));spacing=list(map(F,c['root_spacing']))
        equal=True;maximum=F(0)
        for cell in c['source_geometry']:
            for axis in range(2):
                h=spacing[axis]/2**cell['level']
                for side in range(2):
                    exact=origins[axis]+(cell['index'][axis]+side)*h
                    delta=abs(F(cell['edges'][2*axis+side])-exact)
                    maximum=max(maximum,delta);equal &= delta==0
        for face in c['faces']:
            i=face['left'] if face['left']>=0 else face['right']
            cell=c['source_geometry'][i]
            for axis in range(2):
                shift=F(face['boundary_side']%2) if axis==face['axis'] else F(1,2)
                exact=origins[axis]+(cell['index'][axis]+shift)*spacing[axis]/2**cell['level']
                delta=abs(F(face['center'][axis])-exact)
                maximum=max(maximum,delta);equal &= delta==0
        # Exact equality is necessary. Declining proof can also be conservative.
        scopes=[bool(f['root_scope']) for f in c['faces']]
        assert all(s==bool(c['root_exact']) for s in scopes)
        if any(scopes):assert equal and maximum==0
        if c['origin']==.3:assert not any(scopes) and maximum>0
        else:assert all(scopes) and equal
        certified+=int(all(scopes));declined+=int(not any(scopes))
        ncell+=len(c['source_geometry']);nface+=len(c['faces'])
        rows.append({'radialOrigin':c['origin'],'mixed':bool(c['mixed']),'zeroDensity':bool(c['zero']),
            'cells':len(c['source_geometry']),'exteriorFaces':len(c['faces']),
            'rootScopeCertified':all(scopes),'maximumExactCoordinateDifference':float(maximum)})
    result={'status':'PASS','cases':len(rows),'cells':ncell,'exteriorFaces':nface,
        'certifiedCases':certified,'declinedRoundedCases':declined,'rows':rows,
        'probeRecordSha256':hashlib.sha256(a.probe_record.read_bytes()).hexdigest(),
        'scope':'Actual current ring producer exact root source edges and exterior observer identity',
        'limitations':['Exact-coordinate subset only; rounded/unsupported geometry remains Unknown',
            'Not physical Poisson/force or complete RZ acceptance; no changed source coordinates',
            'No simulation, CUDA or long-run evidence; raw arrays remain local']}
    a.output.parent.mkdir(parents=True,exist_ok=True)
    a.output.write_text(json.dumps(result,indent=2)+'\n')
    print('RZ_ROOT_PRODUCER_FRACTION_PASS',len(rows),ncell,nface,certified,declined)
if __name__=='__main__':main()
