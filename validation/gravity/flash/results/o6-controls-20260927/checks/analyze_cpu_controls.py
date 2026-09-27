from pathlib import Path
import json,sys,statistics
import numpy as np
import h5py
r=Path('/home/shiroakane/.codex/worktrees/compute-optim/ARCH')
sys.path.insert(0,str(r/'validation/gravity/flash'))
import run_comparison as run
import compare_cellular as cellular
cellular.FIELDS.update(energy=('ENER','ener'))
out=r/'output'
def data(name):return json.loads((out/(name+'.json')).read_text())
def first(m,case):return Path(next(row['output'] for row in m['records'] if row['case']==case and row['backend']=='arch'))
def compare_cpu(reference,candidate,case):
 if not case.startswith('snia'):return run.compare_saved_backend(reference,candidate,case)
 sys.path.insert(0,str(r/'validation/gravity/curved'))
 from compare_backends import compare_plot,verify
 def physical_input(folder):
  return '\n'.join(line for line in (folder/'input.par').read_text().splitlines() if line.split('=',1)[0].strip() not in ('out_dir','compute_backend'))
 assert physical_input(reference)==physical_input(candidate)
 for folder in (reference,candidate):
  plans=list(folder.glob('*_backend_plan.txt'))
  assert len(plans)==1 and 'resolved=cpu\n' in plans[0].read_text()
 plots=[sorted(folder.glob('*_plt_*.h5')) for folder in (reference,candidate)]
 return {'reference':verify(case+'-reference',reference,5),'candidate':verify(case+'-candidate',candidate,5),
  'initial':compare_plot(plots[0][0],plots[1][0],case+'-initial'),
  'final':compare_plot(plots[0][1],plots[1][1],case+'-final')}
old=json.loads((r/'validation/gravity/flash/results/o6-acceptance-20260927/final/o6view_measurements.json').read_text())
base=data('o6qualified');variants=data('o6qualified_davis')
results={'compiler_comparison':{},'default_regression':{},'davis_science':{},'cpu_thread_parity':{}}
for name in ('o6weights_O1_davis','o6weights_O3_davis','o6weights_O3_errno_davis'):
 m=data(name);wall=[x['wall_seconds'] for x in m['records']]
 results['compiler_comparison'][name]={'samples':wall,'median':statistics.median(wall)}
origin=first(data('o6weights_O1_davis'),'fine')
for name in ('o6weights_O3_davis','o6weights_O3_errno_davis'):
 results['compiler_comparison'][name]['fields']=run.compare_saved_backend(origin,first(data(name),'fine'),'fine')
for case in ('sod','jeans64','jeans128','a','b','noburn','fine'):
 results['default_regression'][case]=run.compare_saved_backend(first(old,case),first(base,case),case)
for case in ('a','b','noburn','fine'):
 afolder=first(base,case);bfolder=first(variants,case)
 config=dict(line.split('=',1) for line in (afolder/'input.par').read_text().splitlines() if '=' in line and not line.lstrip().startswith('#'))
 cells=[int(config['nblockx'+str(axis)])*16*2 for axis in (1,2)]
 widths=[(float(config['x'+str(axis)+'_max'])-float(config['x'+str(axis)+'_min']))/count for axis,count in zip((1,2),cells)]
 assert widths[0]==widths[1] and float(config['x1_min'])==float(config['x2_min'])==0
 shape=tuple(reversed(cells));spacing=widths[0]
 a,na,ta,_=cellular.arch_plot(next(afolder.glob('*plt_0001.h5')),shape,spacing)
 b,nb,tb,_=cellular.arch_plot(next(bfolder.glob('*plt_0001.h5')),shape,spacing)
 assert ta==tb
 assert all(np.all(np.isfinite(field)) for field in b.values())
 assert all(np.min(b[key])>0 for key in ('density','pressure','temperature'))
 entry={'physical_time_seconds':ta,'default_leaves':na,'davis_leaves':nb,'relative_l1':{}}
 for f in a:entry['relative_l1'][f]=float(np.mean(np.abs(a[f]-b[f]))/max(np.mean(np.abs(a[f])),1e-100))
 for field,label in [('density','mass'),('energy','energy')]:
  entry[label+'_relative']=float(abs(np.sum(a[field])-np.sum(b[field]))/abs(np.sum(a[field])))
 for key in ('helium_4','carbon_12'):
  entry[key+'_mass_relative']=float(abs(np.sum(a['density']*a[key])-np.sum(b['density']*b[key]))/max(abs(np.sum(a['density']*a[key])),1e-100))
 with h5py.File(next(bfolder.glob('*chk_0001.h5'))) as f:
  entry['repairs']=float(f['state_repairs'][0]);assert entry['repairs']==0
  assert np.all(f['Data/rho'][()]>0)
  x=f['Data/X'][()];assert np.all(np.isfinite(x)) and np.min(x)>=0
  entry['composition_normalization_linf']=float(np.max(np.abs(np.sum(x,axis=0)-1)))
  assert entry['composition_normalization_linf']<=1e-12
 entry['passed']=all(entry['relative_l1'][f]<=.01 for f in ('density','pressure','temperature')) and entry['mass_relative']<=1e-3 and entry['energy_relative']<=1e-3 and all(entry[f+'_mass_relative']<=.01 for f in ('helium_4','carbon_12'))
 assert entry['passed'],case
 results['davis_science'][case]=entry
cpu16=data('o6qualified_cpu16');coupled=data('o6qualified_coupled8')
for case in ('a','b','noburn','jeans128','fine','snia2d','snia3d'):
 reference=first(coupled if case.startswith('snia') else base,case)
 results['cpu_thread_parity'][case]=[compare_cpu(reference,Path(row['output']),case) for row in cpu16['records'] if row['case']==case]
old_coupled=json.loads((r/'validation/gravity/flash/results/o6-acceptance-20260927/final/o6view_cpu8_coupled.json').read_text())
for case in ('snia2d','snia3d'):
 results['default_regression'][case]=compare_cpu(first(old_coupled,case),first(coupled,case),case)
results['davis_cpu_thread_parity']=run.compare_saved_backend(first(variants,'fine'),first(data('o6qualified_davis16'),'fine'),'fine')
(out/'o6qualified_science.json').write_text(json.dumps(results,indent=2)+'\n')
print(json.dumps({'compiler':{k:v['median'] for k,v in results['compiler_comparison'].items()},'default_regression':list(results['default_regression']),'davis_max_temperature_l1':max(x['relative_l1']['temperature'] for x in results['davis_science'].values()),'cpu_thread_parity':list(results['cpu_thread_parity'])},indent=2))
