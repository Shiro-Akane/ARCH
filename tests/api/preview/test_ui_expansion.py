"""CPU API, bounded real initial AMR and comparison with the production Driver at t=0."""
import hashlib
import json
import math
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ARCH, ROOT, READER = map(lambda s: Path(s).resolve(), sys.argv[1:4])
del sys.argv[1:4]
ENV = dict(os.environ, OMP_NUM_THREADS='1', CUDA_VISIBLE_DEVICES='')
SOD = 'nblockx1=4\nnblockx2=0\nnblockx3=0\nx_pos=.43\nlrefinemax=3\nrefine_threshold=.1\nderefine_threshold=.01\nmax_blocks=128\nnetwork_name=none\n'

class Expansion(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix='arch-ui-expansion-')
        self.addCleanup(self.tmp.cleanup)
        self.cwd = Path(self.tmp.name)

    def api(self, command, text='', *options, case='Sod', code=0):
        args = [str(ARCH), command]
        if command not in ['--config-schema', '--list-cases', '--preview-capabilities']:
            args += [case, '--config-stdin', '--request-id', 'test-amr']
        args += list(options)
        before = sorted(self.cwd.rglob('*'))
        p = subprocess.run(args, input=text, capture_output=True, text=True,
                           cwd=self.cwd, env=ENV, timeout=45)
        self.assertEqual(p.returncode, code, (p.stdout[-4000:], p.stderr[-2000:]))
        self.assertEqual(before, sorted(self.cwd.rglob('*')), 'API created files')
        self.assertLessEqual(len(p.stdout.encode()), 8*1024*1024)
        obj = json.loads(p.stdout)
        if obj.get('identity'):
            self.assertEqual(obj['identity']['configRevision'], hashlib.sha256(text.encode()).hexdigest())
        return obj

    def cell_text(self):
        return (ROOT/'simulation/Cellular/CellularPreview2D.par').read_text()+f'\neos_table_path={ROOT}/EOS_toolkit/tables/helmholtz/helm_table.dat\nuse_burn=false\nmax_blocks=128\nrefine_threshold=.1\nderefine_threshold=.01\n'

    def test_registry_is_compiled_and_does_not_setup(self):
        out=self.api('--list-cases')
        cases={x['caseId']:x for x in out['cases']}
        self.assertIn('Sedov', cases)
        self.assertIn('CellularDet', cases)
        self.assertNotIn('Cellular', cases)
        self.assertFalse(cases['Sedov']['initialFieldPreview'])
        self.assertTrue(cases['CellularDet']['initialAmrPreview'])
        self.assertEqual(out['setup'], 'not_executed')
        self.assertTrue(all(not c['automaticCustomUnitInference'] for c in cases.values()))

    def test_all_standard_parameters_have_presentation(self):
        out=self.api('--config-schema')
        params={p['key']:p for p in out['parameters']}
        self.assertEqual(len(params), 88)
        for p in params.values():
            self.assertTrue(p['presentation']['description'])
            self.assertTrue(p['presentation']['displayName'])
        self.assertEqual(params['lrefinemax']['presentation']['subgroup'], 'AMR')
        for key in ['max_steps','plt_dt','plt_dstep','chk_dt','chk_dstep']:
            self.assertEqual(params[key]['presentation']['toggle']['offValue'], -1)
        for key in ['max_blocks','regrid_interval']:
            self.assertNotIn('toggle', params[key]['presentation'])
        reflect=params['x1l_boundary_type']['options']['choices']
        self.assertEqual(len(reflect), 3)
        self.assertEqual(reflect[-1]['acceptedNames'], ['reflect','reflecting'])
        bd=next(c for c in params['ode_solver']['options']['choices'] if c['value']=='bd')
        self.assertIn('Deuflhard', bd['displayName'])

    def test_diffusion_channels_and_cgs(self):
        text=SOD+'use_diffusion=true\nuse_thermal_diff=true\nalpha_therm=2\n'
        out=self.api('--inspect-config', text)
        params={p['key']:p for p in out['parameters']}
        self.assertEqual(out['unitSystem'],'cgs')
        self.assertTrue(params['alpha_therm']['applicable'])
        self.assertFalse(params['nu_visc']['applicable'])
        self.assertEqual(params['alpha_therm']['units']['unit'],'cm^2/s')
        self.assertEqual(out['diffusion']['source'],'constant')
        disabled=self.api('--inspect-config',text+'use_diffusion=false\n')
        self.assertFalse(next(p for p in disabled['parameters'] if p['key']=='alpha_therm')['applicable'])
        out=self.api('--inspect-config',SOD+'eos_type=helmholtz\nuse_diffusion=true\n')
        self.assertEqual(set(out['diffusion']['forbiddenExplicitKeys']),{'alpha_therm','nu_visc','D_spec'})
        self.assertFalse(out['diffusion']['modeEditable'])
        self.assertEqual([c['stellarModelSuppliesCoefficient'] for c in out['diffusion']['channels']],[True,False,False])
        self.api('--inspect-config',SOD+'eos_type=helmholtz\nuse_diffusion=true\nalpha_therm=0\n',code=3)

    def test_resource_estimate_scales_and_does_not_load_eos(self):
        for dim in [1,2,3]:
            text=f'nblockx1=2\nnblockx2={int(dim>=2)}\nnblockx3={int(dim==3)}\nlrefinemax=3\neos_type=helmholtz\neos_table_path=/absent\n'
            out=self.api('--amr-resources',text,case='not-registered')
            data=out['data']
            self.assertEqual(out['execution']['setup'],'not_executed')
            self.assertIsNone(data['speciesCount'])
            self.assertEqual([l['fullDomainLeafBlocks'] for l in data['levels']],[2*2**(dim*i) for i in range(4)])
            self.assertTrue(all(l['stateBytesIncludingSpecies'] is None for l in data['levels']))
            self.assertEqual(data['oomPrediction'],'not-provided')
        huge=self.api('--amr-resources','nblockx1=1000\nnblockx2=1000\nnblockx3=1000\nlrefinemax=15\n')
        self.assertTrue(huge['data']['levels'][-1]['overflow'])
        self.assertIsNone(huge['data']['levels'][-1]['fullDomainLeafBlocks'])

    def verify_mesh(self, out, domain):
        leaves=out['data']['leaves']
        self.assertTrue(leaves)
        self.assertEqual(len({b['logicalKey'] for b in leaves}),len(leaves))
        volume=0
        for b in leaves:
            volume+=math.prod(hi-lo for lo,hi in zip(b['lower'],b['upper']))
            for lo,hi,dx,n in zip(b['lower'],b['upper'],b['cellSpacing'],b['cellShape']):
                self.assertAlmostEqual(hi-lo,dx*n)
        self.assertAlmostEqual(volume,math.prod(domain))
        # Face neighbors may differ by at most one level; no overlap in interiors.
        for i,a in enumerate(leaves):
            for b in leaves[i+1:]:
                overlap=[min(ah,bh)-max(al,bl) for al,ah,bl,bh in zip(a['lower'],a['upper'],b['lower'],b['upper'])]
                self.assertFalse(all(x>1e-10 for x in overlap))
                if sum(abs(x)<1e-10 for x in overlap)==1 and all(x>=-1e-10 for x in overlap):
                    self.assertLessEqual(abs(a['level']-b['level']),1)

    def test_real_sod_mesh_and_budget_snapshots(self):
        out=self.api('--preview-amr',SOD)
        self.assertTrue(out['data']['complete'])
        self.verify_mesh(out,[1])
        self.assertGreater(max(b['level'] for b in out['data']['leaves']),0)
        zero=self.api('--preview-amr',SOD+'lrefinemax=0\n')
        self.assertEqual(zero['data']['leafCount'],4)
        limited=self.api('--preview-amr',SOD,'--mesh-max-blocks','6')
        self.assertEqual(limited['status'],'limited')
        self.assertFalse(limited['data']['complete'])
        self.verify_mesh(limited,[1])
        self.assertEqual(limited['data']['configuredMaxBlocks'],128)
        root=self.api('--preview-amr',SOD,'--mesh-max-blocks','1')
        self.assertEqual(root['data']['snapshot'],'none')
        self.assertEqual(root['data']['leaves'],[])
        self.assertEqual(root['status'],'limited')

    def test_cpu_preview_ignores_requested_cuda(self):
        out=self.api('--preview-amr',SOD+'compute_backend=cuda\ncuda_device=999\n')
        self.assertEqual(out['execution']['previewBackend'],'cpu')
        self.assertEqual(out['state']['computeBackendRequested'],'cuda')
        self.assertEqual(out['execution']['timeStepping'],'not_executed')

    def test_invalid_mesh_requests_have_correct_envelope(self):
        for args in [('--mesh-max-blocks','2.5'),('--mesh-memory-mib','999'),('--samples','32')]:
            out=self.api('--preview-amr',SOD,*args,code=2)
            self.assertEqual(out['kind'],'initial-amr-preview')
        self.api('--preview-amr',SOD,case='Sedov',code=4)
        self.api('--preview-amr',SOD+'restart=true\nrestart_file=/absent\n',code=4)

    def test_cellular_mesh_and_resource_species(self):
        out=self.api('--preview-amr',self.cell_text(),case='CellularDet')
        self.verify_mesh(out,[25.6,12.8])
        self.assertTrue(out['data']['complete'])
        self.assertEqual(out['data']['resources']['speciesCount'],19)

    def test_mesh_matches_production_initial_topology(self):
        # tmax=0 calls production initialization/output only, no time evolution.
        for case,text,domain in [('Sod',SOD+'refine_var=DENS,PRES\n',[1]),('CellularDet',self.cell_text(),[25.6,12.8])]:
            with self.subTest(case=case):
                preview=self.api('--preview-amr',text,case=case)
                self.assertTrue(preview['data']['complete'])
                self.verify_mesh(preview,domain)
                outdir=self.cwd/case
                par=self.cwd/(case+'.par')
                par.write_text(text+f'\nsolver=HLLC\ntmax=0\ncompute_backend=cpu\nout_dir={outdir}\nbase_name=reference\nplt_variables=DENS\n')
                run=subprocess.run([str(ARCH),case,str(par)],cwd=self.cwd,env=ENV,capture_output=True,text=True,timeout=90)
                self.assertEqual(run.returncode,0,(run.stdout[-3000:],run.stderr[-2000:]))
                checkpoints=sorted(outdir.rglob('*chk*.h5'))
                self.assertTrue(checkpoints,list(outdir.rglob('*')))
                ref=json.loads(subprocess.check_output([str(READER),str(checkpoints[0])],text=True))
                self.assertEqual(ref['time'],0)
                self.assertEqual(sorted(ref['keys']),sorted(b['logicalKey'] for b in preview['data']['leaves']))

if __name__=='__main__': unittest.main()
