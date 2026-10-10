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


if __name__=='__main__': unittest.main()
