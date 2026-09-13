import copy
import importlib.util
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'validation/backend'))
spec = importlib.util.spec_from_file_location('microphysics_timing',ROOT/'validation/backend/run_microphysics_timing.py')
timing = importlib.util.module_from_spec(spec)
spec.loader.exec_module(timing)


class MicrophysicsTimingTests(unittest.TestCase):
    def test_observer_cannot_enter_formal_timing(self):
        args=['timing','--candidate-source','unused','--candidate-build','unused',
              '--baseline-source','unused','--baseline-build','unused','--output-root','unused',
              '--preload','unused-observer.so']
        with patch.object(sys,'argv',args),patch.dict(os.environ,{},clear=True):
            with self.assertRaisesRegex(RuntimeError,'instrumented runs cannot'):
                timing.main()

    def test_uncontrolled_preload_is_rejected_before_execution(self):
        args=['timing','--candidate-source','unused','--candidate-build','unused',
              '--baseline-source','unused','--baseline-build','unused','--output-root','unused']
        with patch.object(sys,'argv',args),patch.dict(os.environ,{'LD_PRELOAD':'unknown.so'}):
            with self.assertRaisesRegex(RuntimeError,'inherited LD_PRELOAD'):
                timing.main()

    def test_table_content_must_match_even_when_parameters_match(self):
        left={'case':dict(parameter_file='/base/run.par',parameter_sha256='par',
            scientific_overrides={},eos_type='helmholtz',dependencies=[dict(
                parameter='eos_table_path',path='/base/table',sha256='table')])}
        right=copy.deepcopy(left)
        right['case']['parameter_file']='/candidate/run.par'
        right['case']['dependencies'][0]['path']='/candidate/table'
        self.assertTrue(timing.comparable_inputs(left,right))
        right['case']['dependencies'][0]['sha256']='different'
        self.assertFalse(timing.comparable_inputs(left,right))

    def test_original_budgets_and_input_are_kept(self):
        original = json.loads((ROOT/'validation/backend/cases.json').read_text())['cases']
        for order in (1,2):
            prototype = next(c for c in original if c['id'] == f'diffusion_rkl{order}_n128')
            case, = timing.make_cases([f'diffusion_rkl{order}'],[32])
            for key in ('input','reduction_policy','qualification','scientific_time'):
                self.assertEqual(case[key],prototype[key])
            self.assertNotIn('qualification_group',case)
            self.assertEqual(case['overrides']['nblockx1'],'32')

    def test_all_burn_routes_share_physics_budget(self):
        cases = timing.make_cases(['burn_be_nr','burn_bd','burn_ros4'],[8,32])
        self.assertEqual(len(cases),6)
        for case in cases:
            self.assertEqual(case['input'],'validation/burn/inputs/bd.par')
            self.assertEqual(case['reduction_policy'],dict(rtol=2e-8,atol=1e-12))
            self.assertEqual(case['scientific_time'],1e-10)
            self.assertEqual(case['plan_policy']['ode'],case['overrides']['ode_solver'])

    def test_coupled_timing_keeps_physical_transport_and_runtime_amr(self):
        cases=timing.make_cases(['coupled_bd_rkl1','coupled_ros4_rkl2'],[8])
        for case in cases:
            self.assertEqual(case['overrides']['lrefinemax'],'1')
            self.assertEqual(case['overrides']['use_species_diff'],'true')
            self.assertNotIn('D_spec',case['overrides'])
            self.assertNotIn('ode_rtol',case['overrides'])
            self.assertEqual(case['input'],'validation/amr/inputs/burn_enuc_amr.par')

    def test_unknown_or_invalid_workload_rejected(self):
        for module,count in [('diffusion_magic',8),('burn_unknown',8),('burn_bd',0)]:
            with self.assertRaises(RuntimeError): timing.make_cases([module],[count])

    def test_timer_preserves_exit_and_logs(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp)
            result = timing.timed_process([sys.executable,'-c','print("witness"); raise SystemExit(7)'],ROOT,os.environ,path,10)
            self.assertEqual(result['returncode'],7)
            self.assertFalse(result['timed_out'])
            self.assertGreater(result['arch_wall_seconds'],0)
            self.assertIn('witness',(path/'arch.stdout').read_text())

    def test_timer_bounded_timeout_is_not_a_success(self):
        with tempfile.TemporaryDirectory() as tmp:
            result = timing.timed_process([sys.executable,'-c','import time; time.sleep(10)'],ROOT,os.environ,Path(tmp),0.05)
            self.assertTrue(result['timed_out'])
            self.assertNotEqual(result['returncode'],0)

    def lane(self):
        return dict(checkpoint='checkpoint.h5',directory='lane',metadata=dict(step=10),
            effective_parameters=dict(compute_backend='cpu',out_dir='a',ode_solver='bd'),
            regrid=dict(records=[dict(macro_step=10,old_blocks=8,new_blocks=8,topology_changed=False,physical_time=1e-10)]))

    def test_comparison_requires_same_work_even_when_fields_match(self):
        case, = timing.make_cases(['burn_bd'],[8])
        reference = self.lane()
        with patch.object(timing.validation,'compare_hdf5_checkpoints',return_value=dict(passed=True)):
            candidate = copy.deepcopy(reference)
            candidate['effective_parameters'].update(compute_backend='cuda',out_dir='b')
            self.assertTrue(timing.compare(case,reference,candidate,Path('validator'))['workload_aligned'])
            for mutate in [lambda c: c['metadata'].update(step=11),
                           lambda c: c['effective_parameters'].update(ode_solver='ros4'),
                           lambda c: c['regrid']['records'][0].update(physical_time=2e-10),
                           lambda c: c['regrid']['records'][0].update(new_blocks=16)]:
                candidate = copy.deepcopy(reference)
                mutate(candidate)
                with self.assertRaises(RuntimeError): timing.compare(case,reference,candidate,Path('validator'))

    def test_field_failure_is_not_qualified_by_equal_work(self):
        case, = timing.make_cases(['burn_bd'],[8])
        with patch.object(timing.validation,'compare_hdf5_checkpoints',return_value=dict(passed=False)):
            with self.assertRaises(RuntimeError): timing.compare(case,self.lane(),self.lane(),Path('validator'))


if __name__ == '__main__': unittest.main()
