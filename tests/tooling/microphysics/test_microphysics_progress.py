import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT/'validation/backend'))
import run_microphysics_progress as progress


class ProgressObservationTests(unittest.TestCase):
    def test_only_flushed_step_protocol_and_honest_windows(self):
        p = progress.Progress()
        p.observe('1 1e-5 1e-5 2e-5\n',1.)
        self.assertFalse(p.steps)
        p.observe('Step    Time           dt             dt_hydro\n',2.)
        p.observe('1 1e-5 1e-5 2e-5\n',3.)
        p.observe('[CUDA] resident block count: 42\n',3.5)
        p.observe('2 2e-5 1e-5 2e-5 3e-5 4e-5\n',4.)
        result = p.result(5.)
        self.assertEqual(result['startup_io_and_first_step_seconds'],3.)
        self.assertEqual(result['post_first_step_evolution_seconds'],1.)
        self.assertEqual(result['final_output_and_exit_seconds'],1.)
        self.assertEqual(result['evolution_steps'],1)
        with self.assertRaises(RuntimeError): p.observe('2 2e-5 1e-5 2e-5\n',4.5)

    def test_formal_wrapper_rejected(self):
        with patch.object(sys,'argv',['progress']):
            with self.assertRaisesRegex(RuntimeError,'requires --pilot'): progress.main()

    def test_real_process_output_is_preserved(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp)
            command=[sys.executable,'-c',
                'print("Step Time dt dt_hydro",flush=True); print("1 1e-5 1e-5 2e-5",flush=True)']
            result=progress.observed_process(command,ROOT,os.environ,path,10)
            self.assertEqual(result['returncode'],0)
            self.assertFalse(result['timed_out'])
            self.assertEqual(result['progress_observation']['steps'][0]['step'],1)
            self.assertIn('1 1e-5 1e-5 2e-5',(path/'arch.stdout').read_text())
            self.assertGreaterEqual(result['progress_observation']['final_output_and_exit_seconds'],0)


if __name__ == '__main__': unittest.main()
