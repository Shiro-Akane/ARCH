"""Protocol checks for the recorder regression; no simulation is executed."""
import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import h5py
import numpy as np

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    'predictive_amr_validation', ROOT/'validation/backend/validate_predictive_amr.py')
module = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(module)


class RecorderValidationTests(unittest.TestCase):
    def test_backend_and_boolean_parameters_use_current_contract(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)
            for backend in ('cpu','cuda'):
                for enabled in (False,True):
                    output=root/f'{backend}-{enabled}'
                    with patch.object(module.runtime,'run_arch_with_logs',
                                      side_effect=RuntimeError('unit-only')):
                        with self.assertRaisesRegex(RuntimeError,'unit-only'):
                            module.run(Path('ARCH'),ROOT,output,1,enabled,6,backend=backend)
                    values=module.runtime.read_parameter_map(output/'input.par')
                    self.assertEqual(values['compute_backend'],backend)
                    self.assertEqual(values['predictive_amr_record'],str(enabled).lower())

    def test_exact_comparison_rejects_changed_or_nonfinite_fields(self):
        with tempfile.TemporaryDirectory() as directory:
            left,right=(Path(directory)/name for name in ('left.h5','right.h5'))
            def checkpoint(path,value):
                with h5py.File(path,'w') as file:
                    file['Data/rho']=np.array([value])
                    for key,value in dict(step=6,time=0.01,dim=1,num_species=1,checkpoint_version=4).items():
                        file.attrs[key]=value
            checkpoint(left,1.)
            checkpoint(right,1.)
            self.assertTrue(module.compare(left,right,True)['datasets']['Data/rho']['array_equal'])
            for value in (np.nextafter(1.,2.),float('nan')):
                checkpoint(right,value)
                with self.assertRaises(AssertionError): module.compare(left,right,True)


if __name__=='__main__': unittest.main()
