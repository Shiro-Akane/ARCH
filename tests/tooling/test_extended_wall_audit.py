"""Synthetic parser/gate tests only; no CPU/GPU trajectory is executed."""
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'validation/network'))
import audit_extended_wall as audit


def transcript(network='audit150', pool=8):
    species = {'audit150': 150, 'audit200': 200}[network]
    lines = [f'storage_controls,32,33,{pool}']
    for cells in (32, 33):
        for step in range(16):
            lines += [f'cpu_step,1,{cells},{step},10,1,1.0',
                      f'gpu_step,1,{cells},{step},100,50,2.0']
        lines += [f'state,1,{cells},' + ','.join(['0.5'] * (species + 6))]
    lines += [f'metrics,1,320,32,1e-14,1e-14,1e-4,{pool},36060', 'GENERATED_SPARSE_BURN_PARITY_PASS']
    return '\n'.join(lines)


GUARD = '''MEMORY_GUARD_RESULT min_available_kib=40000000 baseline_swap_kib=9256 peak_swap_kib=9256 guard_stopped=False peak_owned_rss_kib=500000 elapsed_seconds=1234 stop_reason=none
GPU_MEMORY_OBSERVATION scope=whole_device complete=True
SYSTEM_PRESSURE_OBSERVATION scope=linux_system memory_limit_percent=20.0 io_limit_percent=50.0 sustain_seconds=10.0 complete=True
'''


class ExtendedWallAuditTests(unittest.TestCase):
    def test_four_synthetic_transcripts(self):
        for network, pool in audit.EXPECTED:
            with self.subTest(network=network, pool=pool):
                result = audit.validate_transcript(transcript(network, pool), network, pool)
                self.assertEqual(result['cpu_attempts'], 320)
                self.assertEqual(result['diagnostic_gpu_seconds'], 64.0)

    def test_missing_duplicate_or_wrong_steps_rejected(self):
        original = transcript()
        for value in (original.replace('gpu_step,1,33,15,100,50,2.0\n', ''),
                      original.replace('gpu_step,1,33,15,', 'gpu_step,1,33,14,'),
                      original.replace('gpu_step,1,33,15,', 'gpu_step,2,33,15,')):
            with self.subTest(value=value[-200:]), self.assertRaises(ValueError):
                audit.validate_transcript(value, 'audit150', 8)

    def test_completion_marker_required_once(self):
        for value in (transcript().replace('GENERATED_SPARSE_BURN_PARITY_PASS', ''),
                      transcript() + '\nGENERATED_SPARSE_BURN_PARITY_PASS'):
            with self.assertRaises(ValueError): audit.validate_transcript(value, 'audit150', 8)

    def test_physical_error_budget_not_relaxed(self):
        for errors in ('nan,1e-14', '1e-14,inf', '3e-10,1e-14', '1e-14,3e-8'):
            with self.subTest(errors=errors), self.assertRaises(ValueError):
                audit.validate_transcript(transcript().replace('1e-14,1e-14', errors), 'audit150', 8)

    def test_cpu_totals_and_evolution_required(self):
        for value in (transcript().replace('320,32,', '321,32,'),
                      transcript().replace('1e-4,8,', '0,8,')):
            with self.assertRaises(ValueError): audit.validate_transcript(value, 'audit150', 8)

    def test_wrong_pool_or_state_rejected(self):
        with self.assertRaises(ValueError): audit.validate_transcript(transcript(), 'audit150', 32)
        with self.assertRaises(ValueError): audit.validate_transcript(transcript(), 'audit200', 8)

    def test_guard_success_is_not_archive_marker(self):
        audit.validate_guard(GUARD, 'BE_FOLLOWUP_RUNTIME_EXIT 0\n')
        with self.assertRaises(ValueError):
            audit.validate_guard(GUARD, 'BE_FOLLOWUP_ARCHIVE_COMPLETE\n')

    def test_guard_failure_incomplete_or_weakened_rejected(self):
        for text in (GUARD.replace('guard_stopped=False', 'guard_stopped=True'),
                     GUARD.replace('complete=True', 'complete=False'),
                     GUARD.replace('io_limit_percent=50.0', 'io_limit_percent=99.0'),
                     GUARD.replace('peak_swap_kib=9256', 'peak_swap_kib=999999')):
            with self.subTest(text=text), self.assertRaises(ValueError):
                audit.validate_guard(text, 'BE_FOLLOWUP_RUNTIME_EXIT 0\n')


if __name__ == '__main__':
    unittest.main()
