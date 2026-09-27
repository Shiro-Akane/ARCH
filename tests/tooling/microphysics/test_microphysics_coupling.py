import copy
import importlib.util
from pathlib import Path
import sys
from types import SimpleNamespace
import unittest
import numpy as np
from unittest.mock import patch

ROOT=Path(__file__).resolve().parents[3]
spec=importlib.util.spec_from_file_location('microphysics_coupling',ROOT/'validation/backend/verify_microphysics_coupling.py')
module=importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


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

    def balance(self,snapshots):
        reference=SimpleNamespace(nuclear_data=lambda name:dict(arrays=dict(AION=[1.,1.],
            BION=[2.,6.],ZION=[0.5,0.5]),energy_conversion=1.))
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


if __name__=='__main__': unittest.main()
