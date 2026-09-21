"""Repeatable CPU Core timing sample, not a hardware-independent latency test."""
import json
from pathlib import Path
import sys
from client import Client

if len(sys.argv)!=3:
    raise SystemExit('usage: benchmark.py /path/to/ARCH /path/to/ARCH-project')
binary, root = map(lambda p:Path(p).resolve(), sys.argv[1:])
table=root/'EOS_toolkit/tables/helmholtz/helm_table.dat'
cell=(root/'simulation/Cellular/CellularPreview2D.par').read_text()+f'\neos_table_path={table}\nuse_burn=false\n'
hot=(root/'simulation/CooperativeHotspots/CooperativeHotspots.par').read_text()+f'\neos_table_path={table}\n'
report=[]
for case, command, base, edits, options in [
    ('CellularDet','--preview',cell,[('', 'first'),('radiusPerturb=10\n','position'),('tempPerturb=4.2e9\n','temperature')],
     dict(samplesX1=128,samplesX2=128)),
    ('CooperativeHotspots','--inspect-case',hot,[('', 'first'),('hotspot_center_x=48\n','position'),('hotspot_temperature=3.8e9\n','temperature')],{}),
    ('Sod','--preview-amr','nblockx1=4\nnblockx2=0\nnblockx3=0\nnetwork_name=none\nmax_blocks=128\nlrefinemax=2\nx_pos=.43\nrefine_threshold=.1\nderefine_threshold=.01\n',
     [('', 'first'),('x_pos=.6\n','position'),('lrefinemax=3\n','level')],dict(meshMaxBlocks=128)),
]:
    client=Client(binary,root)
    try:
        for edit,label in edits:
            request=dict(command=command,caseId=case,configText=base+edit,requestId=case+'-'+label,**options)
            for event in client.request(request):
                if event['kind']=='preview-session-result':
                    if event['exitCode']!=0:
                        raise RuntimeError(event)
                    report.append(dict(case=case,command=command,edit=label,options=options,
                        elapsedMilliseconds=event['elapsedMilliseconds'],stages=event['stages'],
                        resources=event['resources'],sampleEvaluation=event['resources'].get('sampleEvaluation'),
                        status=event['response']['status']))
    finally:
        client.close()
print(json.dumps(report,ensure_ascii=False,indent=2))
