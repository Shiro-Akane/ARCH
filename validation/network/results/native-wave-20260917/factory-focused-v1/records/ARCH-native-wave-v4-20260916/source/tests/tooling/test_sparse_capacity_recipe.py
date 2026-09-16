import argparse
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'validation/network'))
import run_sparse_capacity as capacity


class SparseCapacityRecipeTests(unittest.TestCase):
    def test_original_gate_command_preserved(self):
        self.assertEqual(capacity.trajectory_command('probe','be_nr',2,[2,3],4),
            ['probe','1e7','3e9','1e-10','1e8','1e-7','4','--ode','be_nr',
             '--storage-cells','2','3','--pool-cells','2','c12=0.5','o16=0.5'])

    def test_longer_trajectory_does_not_change_accuracy_or_initial_state(self):
        original=capacity.trajectory_command('probe','bd',8,[32,33],4)
        extended=capacity.trajectory_command('probe','bd',8,[32,33],16,1e-9)
        self.assertEqual([i for i,(a,b) in enumerate(zip(original,extended)) if a!=b],[3,6])
        for bad in ('0','-1','nan','inf'):
            with self.assertRaises(argparse.ArgumentTypeError): capacity.positive_duration(bad)


if __name__ == '__main__': unittest.main()
