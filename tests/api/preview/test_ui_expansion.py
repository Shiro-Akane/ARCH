"""CPU API, bounded real initial AMR and comparison with the production Driver at t=0."""
import hashlib
import json
import math
import os
import re
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ARCH, ROOT, READER = map(lambda s: Path(s).resolve(), sys.argv[1:4])
del sys.argv[1:4]
ENV = dict(os.environ, OMP_NUM_THREADS='1', CUDA_VISIBLE_DEVICES='')
def edit(text, **values):
    for key, value in values.items():
        text = re.sub(rf"^{re.escape(key)}\s*=.*\n?", "", text, flags=re.MULTILINE)
        text += f"\n{key}={value}\n"
    return text

SOD = edit((ROOT/'simulation/Sod/Sod.par').read_text(), nblockx1=4,
    nblockx2=0, nblockx3=0, x_pos='.43', lrefinemax=3,
    refine_threshold='.1', derefine_threshold='.01', max_blocks=128, network_name='none')


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
        return edit((ROOT/'simulation/Cellular/CellularPreview2D.par').read_text(), eos_table_path=f'{ROOT}/EOS_toolkit/tables/helmholtz/helm_table.dat', use_burn='false', max_blocks=128, refine_threshold='.1', derefine_threshold='.01')

    def test_registry_is_compiled_and_does_not_setup(self):
        out=self.api('--list-cases')
        cases={x['caseId']:x for x in out['cases']}
        self.assertIn('Sedov', cases)
        self.assertIn('CellularDet', cases)
        self.assertNotIn('Cellular', cases)
        self.assertTrue(cases['Sedov']['initialFieldPreview'])
        self.assertTrue(cases['Sedov']['initialAmrPreview'])
        self.assertTrue(cases['CellularDet']['initialAmrPreview'])
        self.assertEqual(out['setup'], 'not_executed')
        self.assertTrue(all(not c['automaticCustomUnitInference'] for c in cases.values()))

    def test_all_standard_parameters_have_presentation(self):
        out=self.api('--config-schema')
        params={p['key']:p for p in out['parameters']}
        self.assertEqual(len(params), 94)
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
        text=edit(SOD,use_diffusion='true',use_thermal_diff='true',use_viscous_diff='false',use_species_diff='false',diff_integrator='RKL2',diff_cfl='.8',alpha_therm=2)
        out=self.api('--inspect-config', text)
        params={p['key']:p for p in out['parameters']}
        self.assertEqual(out['unitSystem'],'cgs')
        self.assertEqual(params['alpha_therm']['applicability']['state'],'satisfied')
        self.assertEqual(params['nu_visc']['applicability']['state'],'not-applicable')
        self.assertEqual(params['alpha_therm']['units']['unit'],'cm^2/s')
        self.assertEqual(out['diffusion']['source'],'constant')
        disabled=self.api('--inspect-config',edit(text,use_diffusion='false'))
        self.assertEqual(next(p for p in disabled['parameters'] if p['key']=='alpha_therm')['applicability']['state'],'not-applicable')
        helm=re.sub(r'^alpha_therm=.*\n?', '', edit(text,eos_type='helmholtz',eos_table_path='/absent',eos_coulomb_mult=1), flags=re.MULTILINE)
        out=self.api('--inspect-config',helm)
        self.assertEqual(set(out['diffusion']['forbiddenExplicitKeys']),{'alpha_therm','nu_visc','D_spec'})
        self.assertFalse(out['diffusion']['modeEditable'])
        self.assertEqual([c['stellarModelSuppliesCoefficient'] for c in out['diffusion']['channels']],[True,False,False])
        self.api('--inspect-config',edit(helm,alpha_therm=0),code=3)

    def test_resource_estimate_scales_and_does_not_load_eos(self):
        for dim in [1,2,3]:
            text=f'nblockx1=2\nnblockx2={int(dim>=2)}\nnblockx3={int(dim==3)}\nlrefinemax=3\nlrefinemin=0\nmax_blocks=2000\neos_type=helmholtz\neos_table_path=/absent\n'
            out=self.api('--amr-resources',text,case='not-registered')
            data=out['data']
            self.assertEqual(out['execution']['setup'],'not_executed')
            self.assertIsNone(data['speciesCount'])
            self.assertEqual([l['fullDomainLeafBlocks'] for l in data['levels']],[2*2**(dim*i) for i in range(4)])
            self.assertTrue(all(l['stateBytesIncludingSpecies'] is None for l in data['levels']))
            self.assertEqual(data['oomPrediction'],'not-provided')
        huge=self.api('--amr-resources','nblockx1=1000\nnblockx2=1000\nnblockx3=1000\nlrefinemax=15\nlrefinemin=0\nmax_blocks=2000\n')
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
        zero=self.api('--preview-amr',edit(SOD,lrefinemax=0))
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
        out=self.api('--preview-amr',edit(SOD,compute_backend='cuda',cuda_device=999))
        self.assertEqual(out['execution']['previewBackend'],'cpu')
        self.assertEqual(out['state']['computeBackendRequested'],'cuda')
        self.assertEqual(out['execution']['timeStepping'],'not_executed')

    def test_invalid_mesh_requests_have_correct_envelope(self):
        for args in [('--mesh-max-blocks','2.5'),('--mesh-memory-mib','999'),('--samples','32')]:
            out=self.api('--preview-amr',SOD,*args,code=2)
            self.assertEqual(out['kind'],'initial-amr-preview')
        unknown=self.api('--preview-amr',SOD,case='not-registered',code=3)
        self.assertTrue(any(d.get('detailCode')=='UNKNOWN_CASE' for d in unknown['diagnostics']))
        self.assertEqual(unknown['state']['setup'],'not_executed')
        sedov=edit((ROOT/'simulation/Sedov/Sedov.par').read_text(),lrefinemax=0)
        mesh=self.api('--preview-amr',sedov,case='Sedov')
        self.assertEqual(mesh['execution']['timeStepping'],'not_executed')
        self.assertTrue(mesh['data']['complete'])
        self.verify_mesh(mesh,[1,1])
        self.api('--preview-amr',edit(SOD,restart='true',restart_file='/absent'),code=4)

    def test_cellular_mesh_and_resource_species(self):
        out=self.api('--preview-amr',self.cell_text(),case='CellularDet')
        self.verify_mesh(out,[25.6,12.8])
        self.assertTrue(out['data']['complete'])
        self.assertEqual(out['data']['resources']['speciesCount'],19)

    def test_mesh_matches_production_initial_topology(self):
        # tmax=0 calls production initialization/output only, no time evolution.
        for case,text,domain in [('Sod',edit(SOD,refine_var='DENS,PRES'),[1]),('CellularDet',self.cell_text(),[25.6,12.8])]:
            with self.subTest(case=case):
                preview=self.api('--preview-amr',text,case=case)
                self.assertTrue(preview['data']['complete'])
                self.verify_mesh(preview,domain)
                root=ROOT/'studio/.local/integration/amr-production-oracle'
                root.mkdir(parents=True,exist_ok=True)
                evidence=Path(tempfile.mkdtemp(prefix=case+'-',dir=root))
                outdir=evidence/'output'
                par=evidence/(case+'.par')
                par.write_text(edit(text,solver='HLLC',hll_wave_speed='roe',tmax=0,max_steps=-1,compute_backend='cpu',out_dir=outdir,base_name='reference',plt_variables='DENS',plt_dt=-1,plt_dstep=-1,chk_dt=-1,chk_dstep=-1))
                run=subprocess.run([str(ARCH),case,str(par)],cwd=self.cwd,env=ENV,capture_output=True,text=True,timeout=90)
                (evidence/'stdout.log').write_text(run.stdout)
                (evidence/'stderr.log').write_text(run.stderr)
                (evidence/'preview.json').write_text(json.dumps(preview))
                self.assertEqual(run.returncode,0,(run.stdout[-3000:],run.stderr[-2000:]))
                checkpoints=sorted(outdir.rglob('*chk*.h5'))
                self.assertTrue(checkpoints,list(outdir.rglob('*')))
                ref=json.loads(subprocess.check_output([str(READER),str(checkpoints[0])],text=True))
                (evidence/'summary.json').write_text(json.dumps({'scope':'t=0 initial topology only; no evolution acceptance','case':case,'time':ref['time'],'productionKeys':sorted(ref['keys']),'previewKeys':sorted(b['logicalKey'] for b in preview['data']['leaves']),'inputSha256':hashlib.sha256(par.read_bytes()).hexdigest(),'binarySha256':hashlib.sha256(ARCH.read_bytes()).hexdigest()},indent=2))
                self.assertEqual(ref['time'],0)
                self.assertEqual(sorted(ref['keys']),sorted(b['logicalKey'] for b in preview['data']['leaves']))

if __name__=='__main__': unittest.main()
