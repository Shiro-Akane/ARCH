import copy
import importlib.util
from pathlib import Path
import sys
from types import SimpleNamespace
import unittest
import numpy as np
from unittest.mock import patch
import h5py
import json
import tempfile

ROOT=Path(__file__).resolve().parents[3]
spec=importlib.util.spec_from_file_location('microphysics_coupling',ROOT/'validation/backend/verify_microphysics_coupling.py')
module=importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
coupled_spec=importlib.util.spec_from_file_location('coupled_endpoint',ROOT/'validation/gravity/curved/verify_coupled.py')
coupled=importlib.util.module_from_spec(coupled_spec)
coupled_spec.loader.exec_module(coupled)


class CoupledMicrophysicsTests(unittest.TestCase):
    def test_active_enuc_requires_binding_accepted_step(self):
        limits={2:(1.234567e-16,1e-16)}
        line='2 2.234567e-16 1.23457e-16 1e-8 6.172835e-17 1e-5'
        self.assertEqual(module.active_enuc_steps(line,limits,2),[2])
        for text in ('', '0 0 2e-16 1e-8 2e-16 1e-5',
                     '2 1e-16 2e-16 1e-8 1e-16 1e-5',
                     '2 1e-16 2e-16 nan 1e-16 1e-5'):
            with self.assertRaises((RuntimeError,ArithmeticError)):
                module.active_enuc_steps(text,limits,2)
        with self.assertRaises(RuntimeError):
            module.active_enuc_steps(line,limits,1)
        with self.assertRaises(RuntimeError):
            module.active_enuc_steps(line,{},2)

    def test_matrix_has_all_methods_and_unchanged_restart_budget(self):
        cases=module.cases()
        self.assertEqual(len(cases),6)
        self.assertEqual({(c['overrides']['ode_solver'],c['overrides']['diff_integrator']) for c in cases},
                         {(m,r) for m in ('be_nr','bd','ros4') for r in ('RKL1','RKL2')})
        for case in cases:
            self.assertEqual(case['reduction_policy'],module.restart.comparison_policy())
            self.assertEqual(case['overrides']['use_diffusion'],'true')
            self.assertNotIn('ode_rtol',case['overrides'])
            # Helm transport owns the physical coefficients; constants are a
            # different EOS mode and production correctly rejects them here.
            for key in ('D_spec','alpha_therm','nu_visc'):
                self.assertNotIn(key,case['overrides'])

    def snapshots(self):
        return [dict(mass=1.,energy=10.,rhoX=[0.5,0.5]),dict(mass=1.,energy=10.4,rhoX=[0.4,0.6])]

    def test_all_physical_transport_is_a_separate_input_matrix(self):
        for original,full in zip(module.cases(),module.cases(True)):
            self.assertEqual(full['id'],original['id']+'_all_transport')
            self.assertEqual(full['reduction_policy'],original['reduction_policy'])
            for key in ('use_species_diff','use_thermal_diff','use_viscous_diff'):
                self.assertEqual(full['overrides'][key],'true')
            for key in ('D_spec','alpha_therm','nu_visc','ode_rtol','ode_atol'):
                self.assertNotIn(key,full['overrides'])

    def test_amr_pointwise_quality_preserves_original_gates(self):
        arrays=dict(rho=np.ones((3,16)),eng=np.ones((3,16)),
                    rhoX=np.full((2,3,16),0.5))
        self.assertEqual(module.field_quality(arrays)['species_sum_absolute'],0.)
        for key,value in [('rho',0.),('eng',-1.),('rhoX',0.7),('eng',float('nan'))]:
            wrong=copy.deepcopy(arrays)
            wrong[key].flat[0]=value
            with self.assertRaises(ArithmeticError): module.field_quality(wrong)

    def balance(self,snapshots,rest=12.):
        reference=SimpleNamespace(burn_energy_data=lambda name:dict(arrays=dict(AION=[1.,1.],
            BION=[2.,6.],ZION=[0.5,0.5]),energy_conversion=1.,
            burn_energy_weights=[rest-2.,rest-6.],burn_energy_conversion=-1.,
            burn_energy_basis='nuclear_mass'))
        with patch.dict(sys.modules,{'nse_reference':reference}), \
             patch.object(module.runtime,'read_conservation_metrics',side_effect=snapshots):
            return module.balance(Path('validator'),Path('initial'),Path('final'),Path('par'))

    def test_source_aware_balance_accepts_nonzero_nuclear_energy(self):
        result=self.balance(self.snapshots())
        self.assertLess(result['energy_relative'],1e-14)
        self.assertEqual(result['mass_relative'],0.)

    def test_unaccounted_energy_mass_or_charge_fail(self):
        for key,value in [('energy',10.41),('mass',1.01),('rhoX',[0.4,0.61]),('energy',float('nan'))]:
            records=self.snapshots()
            records[1][key]=value
            with self.assertRaises(ArithmeticError): self.balance(records)

    def test_zero_burn_cannot_qualify_coupled_case(self):
        initial=self.snapshots()[0]
        with self.assertRaises(ArithmeticError): self.balance([initial,copy.deepcopy(initial)])

    def test_binding_only_balance_cannot_hide_rest_mass_drift(self):
        records=self.snapshots()
        records[1]['rhoX'][1]+=4e-13
        records[1]['mass']+=4e-13
        # This satisfies the old binding-only first-law/mass/charge window,
        # but omits an energy contribution amplified by the rest-mass scale.
        dx=np.array(records[1]['rhoX'],dtype=np.longdouble)-np.array(records[0]['rhoX'],dtype=np.longdouble)
        old_q=np.sum(dx*np.array([2.,6.],dtype=np.longdouble))
        self.assertLess(abs(np.longdouble(10.4)-np.longdouble(10.)-old_q)/10.4,1e-12)
        self.assertLess(abs(sum(dx)),1e-12)
        with self.assertRaisesRegex(ArithmeticError,'first-law'):
            self.balance(records,rest=1e8)

    def test_endpoint_law_handles_cell_arrays_and_rejects_wrong_species(self):
        data=dict(arrays=dict(AION=[1.,1.]),burn_energy_weights=[10.,6.],burn_energy_conversion=-1.)
        result=module.nuclear_energy_delta(data,np.array([[-.1,-.2],[.1,.2]]))
        np.testing.assert_allclose(result,[.4,.8],rtol=0,atol=1e-16)
        for delta in ([0.], [[0.,0.]], [float('nan'),0.]):
            with self.assertRaises(ArithmeticError): module.nuclear_energy_delta(data,delta)

    def test_frozen_network_energy_conventions_are_distinct(self):
        import nse_reference
        alpha=nse_reference.burn_energy_data('aprox13')
        binding=nse_reference.burn_energy_data('iso7')
        self.assertEqual(alpha['burn_energy_basis'],'nuclear_mass')
        self.assertLess(alpha['burn_energy_conversion'],0.)
        self.assertEqual(binding['burn_energy_basis'],'binding_energy')
        self.assertEqual(binding['burn_energy_weights'],binding['arrays']['BION'])
        self.assertEqual(binding['burn_energy_conversion'],binding['energy_conversion'])


class CoupledEndpointObserverTests(unittest.TestCase):
    """Endpoint-mode audit semantics for ordered multi-sample plot runs."""

    def setUp(self):
        self.temporary=tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.directory=Path(self.temporary.name)/'run'

    def write_plot(self,name,time,level_counts,*,e_nuc=1.5,nonfinite=None,nonpositive=False):
        self.directory.mkdir(parents=True,exist_ok=True)
        levels=np.concatenate([np.full(count,level,dtype=np.int32) for level,count in level_counts])
        values={'DENS':1e7,'PRES':1e24,'TEMP':1e9,'ENER':-1e15,'ENUC':e_nuc,
                'GPOT':-1e19,'GACX':0.,'GACY':0.,'c12':.5,'o16':.5}
        with h5py.File(self.directory/name,'w') as handle:
            handle.attrs['time']=float(time)
            handle.attrs['dim']=2
            handle.create_dataset('Grid/level',data=levels)
            for field,value in values.items():
                data=np.full(levels.shape,value)
                if field==nonfinite:
                    data[0]=np.nan
                if nonpositive and field in ('DENS','TEMP'):
                    data[0]=0.
                handle.create_dataset(f'Data/{field}',data=data)

    def write_driver(self,steps=2):
        self.directory.mkdir(parents=True,exist_ok=True)
        rows=''.join('%d %d 0.5 1e-3 2e-3 1e-6 0.25\n'%(step,step) for step in range(1,steps+1))
        (self.directory/'run_log.dat').write_text('Simulation Done. Total Steps: %d\n'%steps+rows)
        (self.directory/'state_repairs.txt').write_text('events=0\n')
        (self.directory/'gravity_solves.tsv').write_text(
            'iteration\tresidual\ttarget\titerations\n0\t1e-12\t1e-10\t4\n')
        (self.directory/'run_regrid.tsv').write_text('step\ttopology_changed\n1\t1\n')

    def test_endpoint_accepts_three_samples_in_hdf5_time_order(self):
        # Filenames oppose the physical order, so only stored HDF5 time can order these.
        self.write_driver()
        self.write_plot('run_plt_00010.h5',30.,[('0',4),('1',8)])
        self.write_plot('run_plt_00020.h5',10.,[('0',8),('1',4)])
        self.write_plot('run_plt_00030.h5',20.,[('0',6),('1',6)])
        result=coupled.verify('ordered',self.directory,None,expected_time=30.)
        self.assertEqual(result['initial']['time_seconds'],10.)
        self.assertEqual(result['final']['time_seconds'],30.)
        self.assertEqual(result['requested_endpoint'],30.)
        self.assertEqual(result['steps'],2)
        self.assertEqual(result['state_repairs'],0)
        self.assertEqual(len(result['samples']),1)
        self.assertEqual(result['samples'][0]['time_seconds'],20.)
        self.assertEqual(result['samples'][0]['leaves_by_level'],{'0':6,'1':6})
        self.assertEqual(result['samples'][0]['field_min_max']['ENUC'],[1.5,1.5])
        self.assertEqual(result['final']['field_min_max']['DENS'],[1e7,1e7])

    def test_evolving_amr_is_opt_in_for_fully_refined_endpoint(self):
        self.write_driver()
        self.write_plot('run_plt_00010.h5',10.,[('0',1),('1',12)])
        self.write_plot('run_plt_00020.h5',30.,[('1',16)])
        with self.assertRaisesRegex(ValueError,'coarse/fine mixed'):
            coupled.verify('default',self.directory,None,expected_time=30.)
        result=coupled.verify('evolving',self.directory,None,expected_time=30.,evolving_amr=True)
        self.assertEqual(result['initial']['leaves_by_level'],{'0':1,'1':12})
        self.assertEqual(result['final']['leaves_by_level'],{'1':16})
        self.assertEqual(result['topology_changes'],1)
        self.assertEqual(result['steps'],2)
        self.assertTrue(result['evolving_amr'])
        target=Path(self.temporary.name)/'evolving.json'
        with patch.object(sys,'argv',['verify_coupled.py','--endpoint-run',
                                      f'evolving:{self.directory}:30.','--evolving-amr',
                                      '--output',str(target)]):
            coupled.main()
        self.assertEqual(json.loads(target.read_text())['evolving'],result)

    def test_evolving_amr_still_requires_actual_topology_change(self):
        self.write_driver()
        self.write_plot('run_plt_00010.h5',10.,[('0',1),('1',12)])
        self.write_plot('run_plt_00020.h5',30.,[('1',16)])
        (self.directory/'run_regrid.tsv').write_text('step\ttopology_changed\n1\t0\n')
        with self.assertRaisesRegex(ValueError,'no actual AMR refinement'):
            coupled.verify('unchanged',self.directory,None,expected_time=30.,evolving_amr=True)

    def test_evolving_amr_rejects_empty_negative_or_noninteger_levels(self):
        self.write_driver()
        self.write_plot('run_plt_00010.h5',10.,[('0',1),('1',12)])
        self.write_plot('run_plt_00020.h5',30.,[('1',16)])
        for levels in (np.array([],dtype=np.int32),np.array([-1],dtype=np.int32),
                       np.array([1.5],dtype=np.float64)):
            with self.subTest(levels=levels):
                with h5py.File(self.directory/'run_plt_00020.h5','r+') as handle:
                    del handle['Grid/level']
                    handle.create_dataset('Grid/level',data=levels)
                with self.assertRaisesRegex(ValueError,'Invalid active AMR levels'):
                    coupled.verify('invalid',self.directory,None,expected_time=30.,evolving_amr=True)

    def test_evolving_amr_requires_endpoint_amr_semantics(self):
        for steps,keywords in ((2,{}),(None,{'expected_time':30.,'expect_mixed':False})):
            with self.subTest(steps=steps,keywords=keywords):
                with self.assertRaisesRegex(ValueError,'requires endpoint AMR mode'):
                    coupled.verify('invalid',self.directory,steps,evolving_amr=True,**keywords)
        for arguments in (['--run','short:missing:2'],
                          ['--run','short:missing:2','--endpoint-run','long:missing:30.']):
            with self.subTest(arguments=arguments), \
                    patch.object(sys,'argv',['verify_coupled.py','--evolving-amr',*arguments]), \
                    patch.object(coupled,'verify') as observer:
                with self.assertRaises(SystemExit) as failure:
                    coupled.main()
                self.assertEqual(failure.exception.code,2)
                observer.assert_not_called()

    def test_short_mode_still_rejects_three_plots(self):
        self.write_driver()
        self.write_plot('run_plt_00010.h5',10.,[('0',8),('1',4)])
        self.write_plot('run_plt_00020.h5',20.,[('0',8),('1',4)])
        self.write_plot('run_plt_00030.h5',30.,[('0',8),('1',4)])
        with self.assertRaisesRegex(ValueError,'initial and final'):
            coupled.verify('short',self.directory,2)

    def test_endpoint_rejects_duplicate_or_nonfinite_plot_times(self):
        self.write_driver()
        self.write_plot('run_plt_00010.h5',10.,[('0',8),('1',4)])
        self.write_plot('run_plt_00020.h5',20.,[('0',6),('1',6)])
        self.write_plot('run_plt_00030.h5',20.,[('0',6),('1',6)])
        with self.assertRaisesRegex(ValueError,'strictly increasing'):
            coupled.verify('duplicate',self.directory,None,expected_time=20.)
        self.write_plot('run_plt_00030.h5',float('nan'),[('0',6),('1',6)])
        with self.assertRaisesRegex(ValueError,'nonfinite'):
            coupled.verify('nonfinite',self.directory,None,expected_time=20.)

    def test_endpoint_must_match_the_latest_physical_output(self):
        self.write_driver()
        self.write_plot('run_plt_00010.h5',10.,[('0',8),('1',4)])
        self.write_plot('run_plt_00020.h5',20.,[('0',6),('1',6)])
        # An earlier sample that matches the target must not satisfy the run while
        # a later physical output exists.
        with self.assertRaisesRegex(ValueError,'endpoint was not reached'):
            coupled.verify('earlier',self.directory,None,expected_time=10.)
        with self.assertRaisesRegex(ValueError,'endpoint was not reached'):
            coupled.verify('unreached',self.directory,None,expected_time=5.)

    def test_malformed_intermediate_plot_is_rejected(self):
        self.write_driver()
        self.write_plot('run_plt_00010.h5',10.,[('0',8),('1',4)])
        self.write_plot('run_plt_00030.h5',30.,[('0',6),('1',6)])
        self.write_plot('run_plt_00020.h5',20.,[('0',6),('1',6)],nonfinite='TEMP')
        with self.assertRaisesRegex(ValueError,'Nonfinite TEMP'):
            coupled.verify('nonfinite',self.directory,None,expected_time=30.)
        self.write_plot('run_plt_00020.h5',20.,[('0',6),('1',6)],nonpositive=True)
        with self.assertRaisesRegex(ValueError,'Nonpositive physical state'):
            coupled.verify('nonpositive',self.directory,None,expected_time=30.)

    def test_intermediate_samples_need_no_enuc_activity_or_mixing(self):
        self.write_driver()
        self.write_plot('run_plt_00010.h5',10.,[('0',8),('1',4)])
        self.write_plot('run_plt_00020.h5',20.,[('0',12)],e_nuc=0.)
        self.write_plot('run_plt_00030.h5',30.,[('0',6),('1',6)])
        result=coupled.verify('inactive',self.directory,None,expected_time=30.)
        self.assertEqual(result['samples'][0]['leaves_by_level'],{'0':12})
        self.assertEqual(result['samples'][0]['field_min_max']['ENUC'],[0.,0.])
        self.assertEqual(result['final']['field_min_max']['ENUC'][1],1.5)

    def test_two_plot_endpoint_call_keeps_original_keys_and_tolerance(self):
        self.write_driver()
        self.write_plot('run_plt_00010.h5',10.,[('0',8),('1',4)])
        self.write_plot('run_plt_00020.h5',30.0000000005,[('0',6),('1',6)])
        result=coupled.verify('two',self.directory,None,expected_time=30.)
        self.assertEqual(result['samples'],[])
        self.assertEqual(result['initial']['time_seconds'],10.)
        self.assertAlmostEqual(result['final']['time_seconds'],30.0000000005)
        for key in ('directory','steps','requested_endpoint','initial','final','state_repairs',
                    'gravity_solves','maximum_residual_over_target','maximum_iterations',
                    'minimum_diffusion_dt_seconds','topology_changes'):
            self.assertIn(key,result)
        with self.assertRaisesRegex(ValueError,'endpoint was not reached'):
            coupled.verify('outside',self.directory,None,expected_time=30.1)

    def test_cli_requires_a_run_and_rejects_duplicate_labels(self):
        with patch.object(sys,'argv',['verify_coupled.py']):
            with self.assertRaises(SystemExit) as failure:
                coupled.main()
        self.assertEqual(failure.exception.code,2)
        with patch.object(sys,'argv',['verify_coupled.py','--run','same:first:2',
                                      '--endpoint-run','same:second:20.']):
            with self.assertRaises(SystemExit) as failure:
                coupled.main()
        self.assertEqual(failure.exception.code,2)
        with patch.object(sys,'argv',['verify_coupled.py','--run','broken','--run','other:a:1']):
            with self.assertRaises(SystemExit) as failure:
                coupled.main()
        self.assertEqual(failure.exception.code,2)

    def test_endpoint_cli_reports_samples(self):
        self.write_driver()
        self.write_plot('run_plt_00010.h5',30.,[('0',4),('1',8)])
        self.write_plot('run_plt_00020.h5',10.,[('0',8),('1',4)])
        self.write_plot('run_plt_00030.h5',20.,[('0',6),('1',6)])
        target=Path(self.temporary.name)/'report.json'
        with patch.object(sys,'argv',['verify_coupled.py','--endpoint-run',
                                      f'ordered:{self.directory}:30.','--output',str(target)]):
            coupled.main()
        report=json.loads(target.read_text())
        self.assertEqual(sorted(report),['ordered'])
        self.assertEqual(report['ordered']['requested_endpoint'],30.)
        self.assertEqual([sample['time_seconds'] for sample in report['ordered']['samples']],[20.])

    def test_ledger_cli_accepts_resolved_plots_from_the_audited_lane(self):
        self.write_driver()
        self.write_plot('run_plt_00010.h5',30.,[('0',4),('1',8)])
        self.write_plot('run_plt_00020.h5',10.,[('0',8),('1',4)])
        initial=self.directory/'run_plt_00020.h5'
        final=self.directory/'run_plt_00010.h5'
        alias=Path(self.temporary.name)/'initial-alias.h5'
        alias.symlink_to(initial)
        target=Path(self.temporary.name)/'ledger.json'
        argv=['verify_coupled.py','--endpoint-run',f'ordered:{self.directory}:30.',
              '--parameters','input.par','--checkpoint-validator','validator',
              '--ledger-pair','final.chk',str(final),
              '--ledger-pair','initial.chk',str(alias),'--output',str(target)]
        def read_sample(_validator,_parameters,_checkpoint,plot):
            with h5py.File(plot,'r') as handle:
                return {'time_seconds':float(handle.attrs['time']),'network':'aprox13'}
        owners=(None,None,lambda _network: {},None)
        with patch.object(sys,'argv',argv), \
                patch.object(coupled,'_project_owners',return_value=owners), \
                patch.object(coupled,'read_ledger_sample',side_effect=read_sample) as reader, \
                patch.object(coupled,'endpoint_ledger',return_value={}) as endpoint:
            coupled.main()
        report=json.loads(target.read_text())['ordered']
        self.assertEqual([sample['time_seconds'] for sample in
                          report['scientific_ledger']['samples']],[10.,30.])
        self.assertEqual(reader.call_count,2)
        before,after,_data=endpoint.call_args.args
        self.assertEqual((before['time_seconds'],after['time_seconds']),(10.,30.))

    def test_ledger_cli_rejects_foreign_plots_with_matching_times_and_contents(self):
        self.write_driver()
        self.write_plot('run_plt_00010.h5',10.,[('0',8),('1',4)])
        self.write_plot('run_plt_00020.h5',30.,[('0',4),('1',8)])
        foreign=Path(self.temporary.name)/'foreign'
        foreign.mkdir()
        for plot in self.directory.glob('*_plt_*.h5'):
            (foreign/plot.name).write_bytes(plot.read_bytes())
        argv=['verify_coupled.py','--run',f'audited:{self.directory}:2',
              '--parameters','input.par','--checkpoint-validator','validator',
              '--ledger-pair','initial.chk',str(foreign/'run_plt_00010.h5'),
              '--ledger-pair','final.chk',str(foreign/'run_plt_00020.h5')]
        with patch.object(sys,'argv',argv), \
                patch.object(coupled,'_project_owners') as owners, \
                patch.object(coupled,'read_ledger_sample') as reader:
            with self.assertRaisesRegex(ValueError,'ledger plot .* was not audited'):
                coupled.main()
        owners.assert_not_called()
        reader.assert_not_called()


import hashlib


_read_parameter_map=module.runtime.read_parameter_map


class LedgerObserverTests(unittest.TestCase):
    """Observational endpoint-ledger semantics on small synthetic publications.

    The checkpoint-validator binary, the frozen burn-energy reader and the
    parameter-map helper are mocked here only; the endpoint nuclear-energy
    helper under test is the real owner loaded by this test module.
    """

    SPECIES=(('h1',1.,0.5),('he4',2.,1.))

    def setUp(self):
        self.temporary=tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.directory=Path(self.temporary.name)
        self.parameters=self.directory/'run.par'
        self.parameters.write_text(
            'geometry=cartesian\n'
            'gravity_type=self\n'
            'gravity_boundary=periodic\n'
            'use_burn=true\n'
            'network_name=aprox13\n'
            'eos_type=helmholtz\n'
            'x1l_boundary_type=periodic\n'
            'x1r_boundary_type=periodic\n'
            'x2l_boundary_type=periodic\n'
            'x2r_boundary_type=periodic\n')

    def burn_data(self,*,digest='a'*64,conversion='b'*64,basis='nuclear_mass',
                  a=None,z=None):
        return {'arrays':{'AION':list(a) if a else [1.,2.],
                          'ZION':list(z) if z else [0.5,1.],
                          'BION':[2.,6.]},
                'burn_energy_weights':[10.,6.],'burn_energy_conversion':-1.,
                'burn_energy_basis':basis,'data_sha256':digest,
                'conversion_sha256':conversion}

    @staticmethod
    def entry(key,rho,eng,gpot,volume,fractions):
        return {'key':key,'rho':rho,'eng':eng,'gpot':gpot,'V':volume,'X':list(fractions)}

    def pair(self,tag,time,entries,*,field_dtype=np.float64,measure_dtype=np.float64,
             field_units=None,raw_config=None,boundary=None,gravity=('self','periodic'),
             network='aprox13',eos_table='c'*64,reverse=False,plot_time=None,
             mom=None,mom_dtype=np.float64,omit_mom=None,mom_shape=None):
        checkpoint=self.directory/f'{tag}.chk.h5'
        plot=self.directory/f'{tag}.plt.h5'
        blocks=len(entries)
        count=len(self.SPECIES)
        rho=np.array([item['rho'] for item in entries])
        eng=np.array([item['eng'] for item in entries])
        gpot=np.array([item['gpot'] for item in entries])
        volume=np.array([item['V'] for item in entries])
        keys=[item['key'] for item in entries]
        levels=np.array([key[0] for key in keys],dtype=np.int32)
        logicals=[np.array([key[axis] for key in keys],dtype=np.int32) for axis in (1,2,3)]
        masses=np.array([[item['X'][index] for item in entries]
                         for index in range(count)]).reshape(count,blocks,1)
        order=np.arange(blocks)[::-1] if reverse else np.arange(blocks)
        with h5py.File(checkpoint,'w') as handle:
            handle.attrs['time']=float(time)
            handle.attrs['step']=1
            handle.attrs['dim']=2
            handle.attrs['geometry']='cartesian'
            handle.attrs['num_species']=count
            handle.attrs['cells_per_block']=1
            handle.attrs['active_network']=network
            handle.attrs['eos_type']='helmholtz'
            handle.attrs['eos_table_sha256']=eos_table
            handle.attrs['boundary_identity']=boundary or (
                'boundary-v1;callbacks.identity;faces='+'periodic;'*4)
            handle.attrs['gravity_type']=gravity[0]
            handle.attrs['gravity_boundary']=gravity[1]
            handle.attrs['burn_enabled']=1
            blocks_group=handle.create_group('Blocks')
            blocks_group.create_dataset('level',data=levels[order])
            for axis in (1,2,3):
                blocks_group.create_dataset(f'logical_x{axis}',
                                            data=logicals[axis-1][order])
            data=handle.create_group('Data')
            data.create_dataset('rho',data=rho[order].reshape(blocks,1))
            data.create_dataset('eng',data=eng[order].reshape(blocks,1))
            data.create_dataset('rhoX',data=masses[:,order,:])
            for name in ('mom_u','mom_v','mom_w'):
                if name==omit_mom:
                    continue
                if mom_shape is not None:
                    values=np.zeros(mom_shape,dtype=np.float64)
                else:
                    values=np.asarray((mom or {}).get(name,np.zeros(blocks)),
                                      dtype=np.float64)[order].reshape(blocks,1)
                data.create_dataset(name,data=values.astype(mom_dtype))
            species_group=handle.create_group('Species')
            species_group.create_dataset('name',data=np.array(
                [name.encode() for name,_,_ in self.SPECIES]))
            species_group.create_dataset('A',data=np.array(
                [value for _,value,_ in self.SPECIES]))
            species_group.create_dataset('Z',data=np.array(
                [value for _,_,value in self.SPECIES]))
        units={'DENS':'g/cm^3','ENER':'erg/cm^3','GPOT':'cm^2/s^2'}
        if field_units:
            units.update(field_units)
        with h5py.File(plot,'w') as handle:
            handle.attrs['time']=float(time if plot_time is None else plot_time)
            handle.attrs['dim']=2
            handle.attrs['geometry']='cartesian'
            handle.attrs['plot_publication_version']='arch-plot-publication-1'
            handle.attrs['plot_publication_state']='complete'
            handle.attrs['plot_identity_state']='recorded'
            handle.create_dataset('Grid/level',data=levels)
            native=handle.create_group('NativeGrid')
            native.attrs['measure_source']='GridMetrics::CellVolume'
            native.attrs['measure_unit']='cm^2'
            native.attrs['measure_normalization']='per_unit_transverse_length'
            native.attrs['native_geometry']='cartesian'
            for axis in (1,2,3):
                native.create_dataset(f'logical_x{axis}',data=logicals[axis-1])
            native.create_dataset('cell_measure',data=volume.astype(measure_dtype))
            identity=handle.create_group('SourceIdentity')
            identity.attrs['raw_config_sha256']=raw_config or hashlib.sha256(
                self.parameters.read_bytes()).hexdigest()
            identity.attrs['eos_type']='helmholtz'
            identity.attrs['eos_table_sha256']=eos_table
            identity.attrs['binary_sha256']='d'*64
            identity.create_dataset('species_names',data=np.array(
                [name.encode() for name,_,_ in self.SPECIES]))
            for name,values in (('DENS',rho),('ENER',eng),('GPOT',gpot)):
                dataset=handle.create_dataset(
                    f'Data/{name}',data=values.reshape(blocks,1).astype(field_dtype))
                dataset.attrs['unit']=units[name]
        return checkpoint,plot

    def sample(self,checkpoint,plot,*,burn=None,metrics=None):
        data=burn or self.burn_data()
        def read_conservation_metrics(validator,checkpoint_path,parameter_file=None):
            with h5py.File(checkpoint_path,'r') as handle:
                num_species=int(handle.attrs['num_species'])
                time=float(handle.attrs['time'])
            result={'measure':'physical_cell_volume','geometry':'cartesian',
                    'mass':1.0,'energy':1.0,'rhoX':[1.0]*num_species,
                    'time':time,'step':1,'blocks':0}
            if parameter_file is not None:
                result['parameter_sha256']=hashlib.sha256(
                    Path(parameter_file).read_bytes()).hexdigest()
            if metrics:
                metrics(result)
            return result
        owners=(read_conservation_metrics,module.nuclear_energy_delta,
                lambda name: data,_read_parameter_map)
        with patch.object(coupled,'_project_owners',return_value=owners):
            return coupled.read_ledger_sample(self.directory/'validator',
                                             self.parameters,checkpoint,plot)

    def endpoint(self,before,after,data=None):
        owners=(None,module.nuclear_energy_delta,None,None)
        with patch.object(coupled,'_project_owners',return_value=owners):
            return coupled.endpoint_ledger(before,after,data or self.burn_data())

    def test_ledger_integrates_stored_cell_volume_not_level_weights(self):
        first=self.entry((0,0,0,0),1e7,1e15,-1e19,1.0,[0.6,0.4])
        second=self.entry((1,2,0,0),2e7,2e15,-2e19,3.0,[0.5,0.5])
        sample=self.sample(*self.pair('volume',10.,[first,second],reverse=True))
        volume=np.array([1.,3.]).astype(np.longdouble)
        rho=np.array([1e7,2e7]).astype(np.longdouble)
        phi=np.array([-1e19,-2e19]).astype(np.longdouble)
        self.assertEqual(sample['blocks'],2)
        self.assertEqual(sample['measure_source'],'GridMetrics::CellVolume')
        self.assertEqual(np.longdouble(sample['mass']),
                         np.sum(volume*rho,dtype=np.longdouble))
        self.assertNotEqual(np.longdouble(sample['mass']),
                            np.sum(rho,dtype=np.longdouble))
        self.assertEqual(np.longdouble(sample['w']),
                         np.longdouble(0.5)*np.sum(volume*rho*phi,dtype=np.longdouble))
        self.assertEqual(np.longdouble(sample['mean_phi']),
                         np.sum(volume*phi,dtype=np.longdouble)/np.sum(volume,
                                                                      dtype=np.longdouble))

    def test_endpoint_ledger_reports_nonzero_nuclear_energy_and_potential(self):
        initial=[self.entry((0,0,0,0),1e7,1e15,-1e19,1.0,[0.6,0.4]),
                 self.entry((1,2,0,0),2e7,2e15,-2e19,3.0,[0.5,0.5])]
        final=[self.entry((0,0,0,0),1e7,1.5e15,-1.5e19,1.0,[0.4,0.6]),
               self.entry((1,2,0,0),2e7,2e15,-2.5e19,3.0,[0.5,0.5]),
               self.entry((0,1,0,0),5e6,1e15,-1e19,2.0,[0.8,0.2])]
        before=self.sample(*self.pair('before',10.,initial,reverse=True))
        after=self.sample(*self.pair('after',20.,final))
        result=self.endpoint(before,after)
        delta=(np.array([np.longdouble(value) for value in after['species_integrals']])
               -np.array([np.longdouble(value) for value in before['species_integrals']]))
        expected=((np.longdouble(after['egas'])-np.longdouble(before['egas']))
                  + (np.longdouble(after['w'])-np.longdouble(before['w']))
                  - module.nuclear_energy_delta(self.burn_data(),delta))
        self.assertEqual(result['residual'],float(expected))
        self.assertNotEqual(result['q'],0.)
        self.assertNotEqual(result['delta_w'],0.)
        self.assertNotEqual(result['delta_egas'],0.)
        self.assertEqual(result['namespace'],'observations_only')
        self.assertFalse(result['scientific_qualified'])
        self.assertIn('upstream FP64',result['uncertainty'])
        self.assertIn('absolute_term_sums',result['roundoff'])
        self.assertEqual(sorted(result['scales']),['energy','mass','q','w'])
        self.assertEqual(len(result['species_mass_change']),2)

    def test_endpoint_keeps_longdouble_difference_beyond_float64_resolution(self):
        big=float(2**53)
        before_entries=[self.entry((0,0,0,0),1e7,big,-1.,1.,[0.6,0.4])]
        after_entries=[self.entry((0,0,0,0),1e7,big,-1.,1.,[0.6,0.4]),
                       self.entry((1,1,0,0),1e7,1.0,-1.,1.,[0.5,0.5])]
        before=self.sample(*self.pair('big_before',10.,before_entries))
        after=self.sample(*self.pair('big_after',20.,after_entries))
        self.assertNotEqual(np.longdouble(before['egas']),
                            np.longdouble(after['egas']))
        self.assertEqual(float(before['egas']),float(after['egas']))
        result=self.endpoint(before,after)
        self.assertEqual(result['delta_egas'],1.0)

    def test_half_ulp_is_conservative_across_zero_subnormal_binade_and_maxfinite(self):
        values=np.array([0.,2.**-1074,1.,2.,np.finfo(np.float64).max])
        half=coupled._half_ulp(values)
        minsubnormal=np.ldexp(np.longdouble(1.),-1075)
        self.assertEqual(half.dtype,np.dtype(np.longdouble))
        self.assertEqual(half[0],minsubnormal)
        self.assertEqual(half[1],minsubnormal)
        self.assertEqual(half[2],np.ldexp(np.longdouble(1.),-53))
        # A binade-boundary value takes the next-larger spacing, i.e. the
        # conservative standard sensitivity rather than an exact certificate.
        self.assertEqual(half[3],np.ldexp(np.longdouble(1.),-52))
        self.assertEqual(half[4],np.ldexp(np.longdouble(1.),970))
        self.assertTrue(np.all(np.isfinite(half)))

    def test_ledger_reports_stored_field_half_ulp_sensitivities(self):
        first=self.entry((0,0,0,0),1e7,1e15,-1e19,1.,[0.6,0.4])
        second=self.entry((1,2,0,0),2e7,2e15,-2e19,3.,[0.5,0.5])
        sample=self.sample(*self.pair('sens',10.,[first,second],reverse=True))
        volume=np.array([1.,3.]).astype(np.longdouble)
        expected=np.sum(volume*coupled._half_ulp(np.array([1e15,2e15])),
                        dtype=np.longdouble)
        self.assertEqual(np.longdouble(sample['egas_sensitivity']),expected)
        self.assertEqual(len(sample['species_integral_sensitivity']),2)
        self.assertTrue(all(np.isfinite(np.longdouble(value))
                            for value in sample['species_integral_sensitivity']))
        self.assertIn('stored-field perturbation only',sample['sensitivity_note'])

    def test_endpoint_q_sensitivity_reuses_the_existing_nuclear_law(self):
        initial=[self.entry((0,0,0,0),1e7,1e15,-1e19,1.,[0.6,0.4])]
        final=[self.entry((0,0,0,0),1e7,1.5e15,-1.5e19,1.,[0.4,0.6])]
        before=self.sample(*self.pair('sens_before',10.,initial))
        after=self.sample(*self.pair('sens_after',20.,final))
        result=self.endpoint(before,after)
        uncertainty=(np.array([np.longdouble(value) for value in
                               before['species_integral_sensitivity']])
                     +np.array([np.longdouble(value) for value in
                                after['species_integral_sensitivity']]))
        expected=np.sum(np.abs(module.nuclear_energy_delta(
            self.burn_data(),np.diag(uncertainty))),dtype=np.longdouble)
        self.assertEqual(np.longdouble(result['sensitivity']['q']),expected)
        self.assertEqual(np.longdouble(result['sensitivity']['delta_egas']),
                         np.longdouble(before['egas_sensitivity'])
                         +np.longdouble(after['egas_sensitivity']))
        self.assertEqual(result['sensitivity']['ratio_state'],'reported')
        self.assertIsNotNone(result['q_over_egas'])
        self.assertIsNotNone(result['q_over_sensitivity'])

    def test_endpoint_requires_and_null_safes_stored_field_sensitivity(self):
        initial=[self.entry((0,0,0,0),1e7,1e15,-1e19,1.,[0.6,0.4])]
        final=[self.entry((0,0,0,0),1e7,1.5e15,-1.5e19,1.,[0.4,0.6])]
        before=self.sample(*self.pair('zs_before',10.,initial))
        after=self.sample(*self.pair('zs_after',20.,final))
        missing=copy.deepcopy(before)
        del missing['egas_sensitivity']
        with self.assertRaisesRegex(ValueError,'mandatory'):
            self.endpoint(missing,after)
        before['egas_sensitivity']='0.0'
        after['egas_sensitivity']='0.0'
        before['species_integral_sensitivity']=['0.0','0.0']
        after['species_integral_sensitivity']=['0.0','0.0']
        result=self.endpoint(before,after)
        self.assertEqual(np.longdouble(result['sensitivity']['q']),np.longdouble(0.))
        self.assertEqual(result['sensitivity']['ratio_state'],
                         'zero_stored_field_sensitivity')
        self.assertIsNone(result['q_over_sensitivity'])

    def test_ledger_requires_binary64_fields_and_canonical_units(self):
        item=[self.entry((0,0,0,0),1e7,1e15,-1e19,1.,[0.6,0.4])]
        with self.assertRaisesRegex(ValueError,'binary64'):
            self.sample(*self.pair('float32field',10.,item,field_dtype=np.float32))
        with self.assertRaisesRegex(ValueError,'binary64'):
            self.sample(*self.pair('float32measure',10.,item,measure_dtype=np.float32))
        with self.assertRaisesRegex(ValueError,'unit'):
            self.sample(*self.pair('badunit',10.,item,field_units={'ENER':'MeV'}))

    def test_ledger_rejects_wrong_config_identity(self):
        item=[self.entry((0,0,0,0),1e7,1e15,-1e19,1.,[0.6,0.4])]
        with self.assertRaisesRegex(ValueError,'raw config identity'):
            self.sample(*self.pair('hash',10.,item,raw_config='e'*64))

    def test_ledger_rejects_duplicate_and_mismatched_leaf_keys(self):
        duplicate=[self.entry((0,0,0,0),1e7,1e15,-1e19,1.,[0.6,0.4]),
                   self.entry((0,0,0,0),2e7,2e15,-2e19,2.,[0.5,0.5])]
        with self.assertRaisesRegex(ValueError,'duplicate logical leaf keys'):
            self.sample(*self.pair('duplicate',10.,duplicate))
        item=[self.entry((0,0,0,0),1e7,1e15,-1e19,1.,[0.6,0.4])]
        checkpoint,plot=self.pair('mismatch',10.,item)
        with h5py.File(checkpoint,'r+') as handle:
            handle['Blocks/logical_x1'][0]=77
        with self.assertRaisesRegex(ValueError,'logical leaf key sets differ'):
            self.sample(checkpoint,plot)

    def test_ledger_rejects_missing_potential_and_nonphysical_measure(self):
        item=[self.entry((0,0,0,0),1e7,1e15,-1e19,1.,[0.6,0.4])]
        checkpoint,plot=self.pair('nophi',10.,item)
        with h5py.File(plot,'r+') as handle:
            del handle['Data/GPOT']
        with self.assertRaisesRegex(ValueError,'missing Data/GPOT'):
            self.sample(checkpoint,plot)
        zero=[self.entry((0,0,0,0),1e7,1e15,-1e19,0.,[0.6,0.4])]
        with self.assertRaisesRegex(ValueError,'cell_measure'):
            self.sample(*self.pair('zerovolume',10.,zero))

    def test_ledger_rejects_nonfinite_and_nonpositive_state(self):
        item=[self.entry((0,0,0,0),1e7,float('nan'),-1e19,1.,[0.6,0.4])]
        with self.assertRaisesRegex(ValueError,'finite'):
            self.sample(*self.pair('naneng',10.,item))
        item=[self.entry((0,0,0,0),0.,1e15,-1e19,1.,[0.6,0.4])]
        with self.assertRaisesRegex(ValueError,'positive'):
            self.sample(*self.pair('zerorho',10.,item))

    def test_ledger_checks_stored_cartesian_momentum_energy(self):
        item=[self.entry((0,0,0,0),1e7,1e15,-1e19,1.,[0.6,0.4])]
        sample=self.sample(*self.pair('momzero',10.,item))
        self.assertEqual(np.longdouble(sample['physical_state']
                                       ['min_specific_internal_energy']),
                         np.longdouble(1e8))
        self.assertEqual(np.longdouble(sample['physical_state']
                                       ['min_species_density']),
                         np.longdouble(0.4))
        # The stored species vector need not close against rho: the observation
        # is reported as a large relative residual and is still accepted.
        relative=np.abs(np.longdouble(0.6)+np.longdouble(0.4)
                        -np.longdouble(1e7))/np.longdouble(1e7)
        self.assertEqual(np.longdouble(sample['physical_state']
                                       ['max_abs_species_sum_minus_rho_relative']),
                         relative)
        self.assertGreater(float(relative),0.5)
        hot=self.sample(*self.pair('momflow',10.,item,mom={'mom_u':np.array([1e7])}))
        self.assertEqual(np.longdouble(hot['physical_state']
                                       ['min_specific_internal_energy']),
                         np.longdouble(1e8)-np.longdouble(2)**-1)

    def test_ledger_rejects_invalid_stored_momentum(self):
        item=[self.entry((0,0,0,0),1e7,1e15,-1e19,1.,[0.6,0.4])]
        with self.assertRaisesRegex(ValueError,'mom_v'):
            self.sample(*self.pair('mommissing',10.,item,omit_mom='mom_v'))
        with self.assertRaisesRegex(ValueError,'binary64'):
            self.sample(*self.pair('momdtype',10.,item,mom_dtype=np.float32))
        with self.assertRaisesRegex(ValueError,'mom_u'):
            self.sample(*self.pair('momshape',10.,item,mom_shape=(2,1)))
        with self.assertRaisesRegex(ValueError,'finite'):
            self.sample(*self.pair('momnan',10.,item,
                                   mom={'mom_u':np.array([float('nan')])}))

    def test_ledger_rejects_nonpositive_physical_internal_energy(self):
        # A finite positive energy density can still leave no internal energy
        # once the stored Cartesian kinetic term is removed.
        item=[self.entry((0,0,0,0),1e7,1e7,-1e19,1.,[0.6,0.4])]
        with self.assertRaisesRegex(ValueError,'specific internal energy'):
            self.sample(*self.pair('momhot',10.,item,
                                   mom={'mom_u':np.array([2e7])}))

    def test_ledger_rejects_unsupported_scope(self):
        item=[self.entry((0,0,0,0),1e7,1e15,-1e19,1.,[0.6,0.4])]
        boundary='boundary-v1;callbacks.identity;faces=reflecting;periodic;periodic;periodic;'
        with self.assertRaisesRegex(ValueError,'unsupported boundary'):
            self.sample(*self.pair('reflecting',10.,item,boundary=boundary))
        with self.assertRaisesRegex(ValueError,'unsupported gravity'):
            self.sample(*self.pair('nocg',10.,item,gravity=('none','periodic')))
        with self.assertRaisesRegex(ValueError,'unsupported ledger network'):
            self.sample(*self.pair('badnet',10.,item,network='aprox99'))

    def test_ledger_rejects_bit_or_shape_mismatch(self):
        item=[self.entry((0,0,0,0),1e7,1e15,-1e19,1.,[0.6,0.4])]
        checkpoint,plot=self.pair('bits',10.,item)
        with h5py.File(checkpoint,'r+') as handle:
            handle['Data/rho'][0,0]=1e7+1
        with self.assertRaisesRegex(ValueError,'bit-exactly'):
            self.sample(checkpoint,plot)
        checkpoint,plot=self.pair('rhoXshape',10.,item)
        with h5py.File(checkpoint,'r+') as handle:
            del handle['Data/rhoX']
            handle.create_dataset('Data/rhoX',data=np.zeros((2,1,2)))
        with self.assertRaisesRegex(ValueError,'rhoX'):
            self.sample(checkpoint,plot)

    def test_ledger_requires_finite_recorded_metrics(self):
        item=[self.entry((0,0,0,0),1e7,1e15,-1e19,1.,[0.6,0.4])]
        with self.assertRaisesRegex(ValueError,'finite recorded number'):
            self.sample(*self.pair('nanmetric',10.,item),
                        metrics=lambda result: result.update({'mass':float('nan')}))
        with self.assertRaisesRegex(ValueError,'required key'):
            self.sample(*self.pair('missingmetric',10.,item),
                        metrics=lambda result: result.pop('time'))
        with self.assertRaisesRegex(ValueError,'physical times differ'):
            self.sample(*self.pair('plottime',10.,item,plot_time=11.))

    def test_endpoint_rejects_mismatched_supplied_identity(self):
        initial=[self.entry((0,0,0,0),1e7,1e15,-1e19,1.,[0.6,0.4])]
        final=[self.entry((0,0,0,0),1e7,1.5e15,-1.5e19,1.,[0.4,0.6])]
        before=self.sample(*self.pair('id_before',10.,initial))
        after=self.sample(*self.pair('id_after',20.,final))
        with self.assertRaisesRegex(ValueError,'nuclear data identity'):
            self.endpoint(before,after,self.burn_data(digest='f'*64))
        with self.assertRaisesRegex(ValueError,'A/Z identity'):
            self.endpoint(before,after,self.burn_data(a=[1.,4.]))
        with self.assertRaisesRegex(ValueError,'strictly increasing'):
            self.endpoint(after,before)
        other=self.sample(*self.pair('id_other',20.,final,eos_table='f'*64))
        with self.assertRaisesRegex(ValueError,'disagree on eos_table_sha256'):
            self.endpoint(before,other)

    def test_ledger_cli_validates_the_optional_branch_flags(self):
        arguments=(['verify_coupled.py','--ledger-pair','a','b'],
                   ['verify_coupled.py','--run','lab:directory:1','--parameters','p'],
                   ['verify_coupled.py','--run','lab:directory:1','--parameters','p',
                    '--checkpoint-validator','v','--ledger-pair','a','b'],
                   ['verify_coupled.py','--run','lab:directory:1','--run','other:directory:1',
                    '--parameters','p','--checkpoint-validator','v',
                    '--ledger-pair','a','b','--ledger-pair','c','d'])
        for argv in arguments:
            with patch.object(sys,'argv',argv):
                with self.assertRaises(SystemExit) as failure:
                    coupled.main()
            self.assertEqual(failure.exception.code,2)


if __name__=='__main__': unittest.main()
