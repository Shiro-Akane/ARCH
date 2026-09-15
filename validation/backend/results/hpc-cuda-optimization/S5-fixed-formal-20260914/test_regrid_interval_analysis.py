"""Synthetic arithmetic/parser tests only; not new physical or GPU samples."""
import copy
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('regrid_analysis', Path(__file__).with_name('analyze_regrid_intervals.py'))
analysis = importlib.util.module_from_spec(spec)
spec.loader.exec_module(analysis)


def samples():
    result = []
    for repeat, (duration, interval) in enumerate(zip((1., 2., 3., 4., 100.), (.1, 1.8, .3, 3.6, 10.))):
        result.append(dict(repeat=repeat, arch_wall_seconds=duration, metadata=dict(step=1),
            directory='synthetic-test-only', regrid=dict(
                records=[dict(macro_step=1, wall_seconds=interval)],
                summary=dict(wall_seconds=interval, records=1, overlaps_backend_trace=True))))
    return result


class ArithmeticContracts(unittest.TestCase):
    def test_median_of_ratios_is_not_ratio_of_medians(self):
        result = analysis.summarize(samples())['metrics']
        self.assertAlmostEqual(result['interval_fraction_percent']['median'], 10.)
        self.assertAlmostEqual(100 * result['interval_seconds']['median'] /
                               result['arch_wall_seconds']['median'], 60.)

    def test_missing_or_duplicate_repeats_rejected(self):
        for data in (samples()[:4], [*samples()[:4], copy.deepcopy(samples()[0])]):
            with self.assertRaisesRegex(ValueError, 'five distinct'):
                analysis.summarize(data)

    def test_changed_existing_summary_rejected(self):
        data = samples()
        data[0]['regrid']['summary']['wall_seconds'] = .2
        with self.assertRaisesRegex(ValueError, 'summary does not match'):
            analysis.summarize(data)

    def test_nonfinite_duration_rejected(self):
        data = samples()
        data[0]['arch_wall_seconds'] = float('nan')
        with self.assertRaisesRegex(ValueError, 'application duration'):
            analysis.summarize(data)

    def test_interval_cannot_exceed_whole_application(self):
        data = samples()
        data[0]['arch_wall_seconds'] = .01
        with self.assertRaisesRegex(ValueError, 'exceed application'):
            analysis.summarize(data)

    def test_initial_runtime_partition(self):
        data = samples()
        for row in data:
            total = row['regrid']['summary']['wall_seconds']
            row['regrid']['records'] = [dict(macro_step=0, wall_seconds=total * .25),
                                        dict(macro_step=1, wall_seconds=total * .75)]
            row['regrid']['summary']['wall_seconds'] = analysis.math.fsum(
                v['wall_seconds'] for v in row['regrid']['records'])
            row['regrid']['summary']['records'] = 2
        for row in analysis.summarize(data)['samples']:
            self.assertAlmostEqual(row['initial_step_zero_seconds'] +
                                   row['positive_step_interval_seconds'], row['interval_seconds'])


if __name__ == '__main__':
    unittest.main()
