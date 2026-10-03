"""Read-only candidate leaf-outline comparison to stored H5, checkpoint and Preview keys."""
import argparse,hashlib,json
from pathlib import Path
import h5py
parser=argparse.ArgumentParser(description=__doc__)
for name in ['plot','checkpoint','preview','response','output']:parser.add_argument('--'+name,required=True)
args=parser.parse_args()
plot=Path(args.plot);checkpoint=Path(args.checkpoint)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
before=sha(plot)
response=json.loads(Path(args.response).read_text())
audit=response['audit'];overview=audit['overview'];outlines=overview['nativeBlocks']
assert audit['file']['sha256']==before
preview=json.loads(Path(args.preview).read_text())
leaves={tuple([b['level'],*b['logicalIndex']]):b for b in preview['data']['leaves']}
max_preview_difference=0.
with h5py.File(plot,'r') as p,h5py.File(checkpoint,'r') as c:
 assert p.attrs['time']==c.attrs['time']==0 and c.attrs['step']==0
 shape=p['Data/DENS'].shape;dim=int(p.attrs['dim']);per=1
 for n in shape[1:]:per*=n
 keys=lambda g,levels:[tuple(int(x) for x in row) for row in zip(levels,g['logical_x1'][:],g['logical_x2'][:],g['logical_x3'][:])]
 pk=keys(p['NativeGrid'],p['Grid/level'][:]);ck=keys(c['Blocks'],c['Blocks/level'][:])
 assert len(set(pk))==len(pk) and set(pk)==set(ck)==set(leaves)
 assert outlines['totalBlocks']==shape[0]
 assert len(outlines['blocks'])==min(shape[0],outlines['limit'])
 assert outlines['complete']==(shape[0]<=outlines['limit'])
 for b in outlines['blocks']:
  index=b['index'];key=tuple([b['level'],*b['logicalCoordinates']])
  assert key==pk[index] and b['logicalKey']=='/'.join(str(x) for x in key)
  assert b['firstCellIndex']==index*per
  assert b['cellShape']==list(reversed(shape[1:]))+[1]*(3-dim)
  for axis in range(3):
   lo=p[f'NativeGrid/x{axis+1}_lower'][index*per:(index+1)*per]
   hi=p[f'NativeGrid/x{axis+1}_upper'][index*per:(index+1)*per]
   assert b['lower'][axis]==float(lo.min()) and b['upper'][axis]==float(hi.max())
   if axis<dim:
    leaf=leaves[key]
    max_preview_difference=max(max_preview_difference,abs(b['lower'][axis]-leaf['lower'][axis]),abs(b['upper'][axis]-leaf['upper'][axis]))
 assert overview['scannedCells']==shape[0]*per
assert sha(plot)==before
result={'scope':'t=0 stored leaf outlines only; no evolution or scientific tolerance acceptance',
 'plotfileSha256':before,'leafBlocks':outlines['totalBlocks'],'emittedBlocks':len(outlines['blocks']),
 'complete':outlines['complete'],'logicalKeysMatchStoredCheckpointAndPreview':True,
 'blockBoundsExactlyMatchStoredNativeBounds':True,'storedShapeAndFirstIndicesMatch':True,
 'maxBoundsAbsDifferenceVsPreview':max_preview_difference,
 'scientificBoundsReview':'reported without adding tolerance','sourceUnchanged':True}
Path(args.output).write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result,indent=2))
