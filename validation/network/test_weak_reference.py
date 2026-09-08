"""Fail-closed evidence-reader controls; use the oracle's scientific Python env.

Run: python -m unittest discover -s validation/network -p test_weak_reference.py
No GPU and no production mathematical implementation are needed.
"""
from contextlib import redirect_stdout
import io
from pathlib import Path
import tempfile
import unittest

from weak_reference import compare_trajectory


class WeakEvidenceContract(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.path = Path(self.directory.name) / "trajectory.csv"
        self.reference = {
            "controls": dict(rho=4e9, temperature=5e8, interval=10, cv=1e8),
            "trajectories": [{"state": [0.5, 0.5, 4e8, -1e12]}]}
        self.lines = ["controls,4000000000,500000000,10,100000000,1e-11",
                      "method,backend,attempts,rejections,closure,state..."]
        self.lines += [f"{method},{backend},10,1,0,0.5,0.5,400000000,-1000000000000"
                       for method in range(3) for backend in range(2)]
        self.lines += ["WEAK_TRAJECTORY_PARITY_PASS"]

    def compare(self, lines):
        self.path.write_text('\n'.join(lines) + '\n')
        with redirect_stdout(io.StringIO()):
            return compare_trajectory(self.reference, self.path)

    def test_complete_evidence(self):
        result = self.compare(self.lines)
        self.assertTrue(result["pass"])
        self.assertEqual(len(result["rows"]), 6)
        self.assertEqual(result["normalized_budget"], 1e-7)

    def test_input_identity_is_required(self):
        variants = [self.lines[1:], self.lines + self.lines[:1],
                    [self.lines[0].replace('500000000', '500000001')] + self.lines[1:],
                    [self.lines[0].replace('1e-11', 'nan')] + self.lines[1:]]
        for lines in variants:
            with self.subTest(lines=lines), self.assertRaises(ValueError):
                self.compare(lines)

    def test_missing_duplicate_unknown_or_failed_paths(self):
        for lines in (self.lines[:-1], self.lines[:2] + self.lines[3:],
                      self.lines + [self.lines[2]],
                      self.lines[:2] + ['9' + self.lines[2][1:]] + self.lines[3:],
                      self.lines + ['WEAK_TRAJECTORY_PARITY_FAIL']):
            with self.subTest(lines=lines), self.assertRaises(ValueError):
                self.compare(lines)

    def test_nonfinite_extent_and_incomplete_solver_evidence(self):
        for bad in ('0,0,0,0,0,0.5,0.5,400000000,-1000000000000',
                    '0,0,10,11,0,0.5,0.5,400000000,-1000000000000',
                    '0,0,10,1,nan,0.5,0.5,400000000,-1000000000000',
                    '0,0,10,1,0,0.5,0.5,nan,-1000000000000',
                    '0,0,10,1,0,0.5,0.5,400000000'):
            with self.subTest(bad=bad), self.assertRaises(ValueError):
                self.compare(self.lines[:2] + [bad] + self.lines[3:])

    def test_scientific_error_is_not_hidden_by_parity(self):
        with self.assertRaises(ArithmeticError):
            self.compare([line.replace('400000000,-1000000000000',
                                       '400000060,-1000000000000') for line in self.lines])

    def test_helm_uses_independent_energy_and_all_six_paths(self):
        self.reference['controls'].update(eos='helmholtz', energy_scale=1.8e18)
        self.lines[0] = 'controls_helm,4000000000,500000000,10,1800000000000000000,1e-11'
        result = self.compare(self.lines)
        self.assertTrue(result['pass'])
        self.assertEqual(result['normalization'], [1.0, 1.0, 5e8, 1.8e18])
        for control in ('controls_helm,4000000000,500000000,10,nan,1e-11',
                        'controls_helm,4000000000,500000000,10,1800000000000000001,0',
                        'controls_helm,4000000000,500000000,10,1800000001000000000,1e-11'):
            with self.subTest(control=control), self.assertRaises(ValueError):
                self.compare([control, *self.lines[1:]])
        with self.assertRaises(ValueError):
            self.compare(self.lines[:2] + self.lines[3:])

    def test_wrong_or_mixed_eos_cannot_qualify(self):
        helm_control = 'controls_helm,4000000000,500000000,10,1800000000000000000,1e-11'
        for lines in ([helm_control, *self.lines[1:]], [*self.lines, helm_control]):
            with self.subTest(lines=lines), self.assertRaises(ValueError):
                self.compare(lines)
        self.reference['controls'].update(eos='helmholtz', energy_scale=1.8e18)
        with self.assertRaises(ValueError):
            self.compare([helm_control, *self.lines])


if __name__ == '__main__':
    unittest.main()
