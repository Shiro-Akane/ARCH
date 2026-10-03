"""Read-only t=0 DENS/native metadata comparison; reports geometry deltas without a scientific tolerance."""
import argparse,json,hashlib,h5py,numpy as np
from pathlib import Path
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--project-root',required=True)
parser.add_argument('--evidence-directories',required=True,help='JSON list of local t=0 evidence directories')
parser.add_argument('--binary',required=True,help='Exact executable used for these outputs')
parser.add_argument('--output',required=True,help='Processed JSON summary only')
args=parser.parse_args()
r=Path(args.project_root).resolve()
binary=Path(args.binary).resolve()
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
def text(v):return v.decode() if isinstance(v,bytes) else str(v)
rows=[]
for d in json.loads(Path(args.evidence_directories).read_text()):
 d=Path(d);old=json.loads((d/'summary.json').read_text());case=old['case']
 plot=next(d.glob('output/*plt*.h5'));chk=next(d.glob('output/*chk*.h5'));par=d/(case+'.par')
 preview=json.loads((d/'preview.json').read_text())
 leaves={tuple([b['level'],*b['logicalIndex']]):b for b in preview['data']['leaves']}
 before=sha(plot)
 with h5py.File(plot,'r') as p,h5py.File(chk,'r') as c:
  assert p.attrs['time']==c.attrs['time']==0
  assert c.attrs['step']==0
  dim=int(p.attrs['dim']);shape=p['Data/DENS'].shape;count=shape[0];per=int(np.prod(shape[1:]))
  def keys(g,levels):
   return [tuple(int(x) for x in row) for row in zip(levels,g['logical_x1'][:],g['logical_x2'][:],g['logical_x3'][:])]
  pk=keys(p['NativeGrid'],p['Grid/level'][:]);ck=keys(c['Blocks'],c['Blocks/level'][:])
  assert len(set(pk))==len(pk)==len(leaves)
  assert set(pk)==set(ck)==set(leaves)
  densities=p['Data/DENS'][:].reshape(count,per);reference=c['Data/rho'][:].reshape(count,per)
  cm={key:i for i,key in enumerate(ck)}
  mismatch=0;max_bound_error=0.;max_center_error=0.;max_measure_error=0.
  measures=p['NativeGrid/cell_measure'][:].reshape(count,per)
  for b,key in enumerate(pk):
   mismatch+=int(np.count_nonzero(densities[b].view(np.uint64)!=reference[cm[key]].view(np.uint64)))
   leaf=leaves[key];sp=leaf['cellSpacing'];activeShape=leaf['cellShape']
   measure=float(np.prod(sp));max_measure_error=max(max_measure_error,float(np.max(np.abs(measures[b]-measure))))
   for axis in range(3):
    lower=p[f'NativeGrid/x{axis+1}_lower'][b*per:(b+1)*per]
    upper=p[f'NativeGrid/x{axis+1}_upper'][b*per:(b+1)*per]
    center=p['Grid/'+['x','y','z'][axis]][b*per:(b+1)*per]
    if axis>=dim:
     assert np.all(lower==0) and np.all(upper==0) and np.all(center==0);continue
    stride=int(np.prod(activeShape[:axis]));i=(np.arange(per)//stride)%activeShape[axis]
    expectedLower=leaf['lower'][axis]+i*sp[axis]
    expectedUpper=leaf['lower'][axis]+(i+1)*sp[axis]
    expectedCenter=leaf['lower'][axis]+i*sp[axis]+.5*sp[axis]
    max_bound_error=max(max_bound_error,float(np.max(np.abs(lower-expectedLower))),float(np.max(np.abs(upper-expectedUpper))))
    max_center_error=max(max_center_error,float(np.max(np.abs(center-expectedCenter))))
  assert mismatch==0
  identity=p['SourceIdentity']
  assert text(identity.attrs['case_id'])==case
  assert text(identity.attrs['raw_config_sha256'])==sha(par)
  assert text(identity.attrs['binary_sha256'])==sha(binary)
  assert text(identity.attrs['eos_type'])==text(c.attrs['eos_type'])
  assert text(identity.attrs['eos_table_sha256'])==(text(c.attrs['eos_table_sha256']) or 'unknown')
  assert [text(v) for v in identity['species_names'][:]]==[text(v) for v in c['Species/name'][:]]
  assert text(identity.attrs['build_id'])=='unknown'
  property_state=text(identity.attrs.get('species_properties_state','unknown'))
  compared_properties=[]
  if property_state=='recorded':
   assert text(identity.attrs['species_properties_version'])=='checkpoint-species-1'
   assert text(identity.attrs['species_properties_source'])=='resolved-runtime-checkpoint-provenance'
   for name in ['A','Z','gamma','Cv']:
    stored=identity['species_'+name];expected=c['Species/'+name]
    assert stored.dtype.kind=='f' and stored.dtype.itemsize==8
    assert stored.shape==expected.shape==(len(identity['species_names']),)
    values=stored[:];reference_values=expected[:]
    assert np.isfinite(values).all()
    assert np.array_equal(values.view(np.uint64),reference_values.view(np.uint64))
    compared_properties.append(name)
  else:
   assert property_state=='unknown'

  rows.append({'case':case,'dimension':dim,'time':0,'step':0,'leafBlocks':count,'storedShape':list(shape),
   'densitySamplesCompared':count*per,'densityBitExactMismatchCount':mismatch,
   'logicalKeysMatchPreviewAndCheckpoint':True,'maxNativeBoundsAbsDifference':max_bound_error,
   'maxCartesianCenterAbsDifference':max_center_error,'maxCellMeasureAbsDifference':max_measure_error,
   'numericalMetadataReview':'reported without adding scientific tolerance',
   'caseRawConfigBinaryEosSpeciesMatch':True,'eosIdentityScope':'policy/table/names; not complete EOS certification',
   'speciesPropertiesState':property_state,'checkpointSpeciesPropertiesBitMatched':compared_properties,'rawConfigSha256':sha(par),'binarySha256':sha(binary),
   'plotfileSha256':before,'plotfileBytes':plot.stat().st_size,
   'localEvidenceDirectory':str(d),'scope':'t=0 DENS/native metadata only; no evolution or all-field acceptance'})
 assert sha(plot)==before
Path(args.output).write_text(json.dumps(rows,indent=2)+'\n')
print(json.dumps(rows,indent=2))
