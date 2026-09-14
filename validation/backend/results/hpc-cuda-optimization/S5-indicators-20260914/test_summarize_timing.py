"""Check summary qualification against the already archived P1/P2 samples."""
import copy
import json
from pathlib import Path
import unittest
from unittest.mock import patch

import summarize_timing as summary


class SummaryTests(unittest.TestCase):
    directory = Path(__file__).resolve().parent.parent / 'P12-kernel-batch-20260914/timing'

    def test_existing_five_module_matrix_roundtrips(self):
        with patch.object(summary, 'MODULES', summary.MODULES[:5]):
            rows = summary.rows(self.directory)
        self.assertEqual(len(rows), 15)
        self.assertTrue(any(values[-2] < 1 for _, values in rows))
        self.assertTrue(any(values[-2] > 1 for _, values in rows))

    def test_pilot_partial_work_mismatch_and_false_stats_fail(self):
        original = json.loads((self.directory / 'formal-burn_bd-v1.json').read_text())
        mutations = (lambda r: r.update(pilot=True), lambda r: r['lanes'].pop(),
            lambda r: r['comparisons'][0].update(workload_aligned=False),
            lambda r: r['comparisons'][0]['fields'].update(passed=False),
            lambda r: r['statistics'][0].update(median=0),
            lambda r: r['statistics'][0].update(population_stdev=0),
            lambda r: r['statistics'].append(r['statistics'][0]))
        for mutate in mutations:
            bad = copy.deepcopy(original)
            mutate(bad)
            with patch.object(summary, 'MODULES', ('burn_bd',)), \
                    patch.object(Path, 'read_text', return_value=json.dumps(bad)):
                with self.assertRaises(RuntimeError):
                    summary.rows(self.directory)


if __name__ == '__main__':
    unittest.main()
