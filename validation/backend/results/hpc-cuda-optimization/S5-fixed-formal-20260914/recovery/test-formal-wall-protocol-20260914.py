"""Local recipe tests only: no compiler, server, ARCH or GPU is invoked."""
from pathlib import Path
import re
import shlex
import unittest

ROOT = Path(__file__).resolve().parent
OLD = (ROOT / 'replay-fixed-timing-20260914.sh').read_text(encoding='utf-8')
NEW = (ROOT / 'run-fixed-formal-worker-v3-20260914.sh').read_text(encoding='utf-8')


def invocation(source):
    logical = source.replace('\\\n', ' ')
    command = next(line.strip() for line in logical.splitlines()
                   if line.strip().startswith('"$python" validation/backend/run_microphysics_timing.py'))
    command = command.split(' > ', 1)[0]
    tokens = shlex.split(command)
    if '${options[@]}' in tokens:
        options = re.search(r'^options=\(([^\n]*)\)$', source, re.M).group(1)
        index = tokens.index('${options[@]}')
        tokens[index:index + 1] = shlex.split(options)
    return tokens


class FormalWallProtocolTests(unittest.TestCase):
    def test_only_runtime_wall_timeout_changes_in_the_scientific_command(self):
        before, after = invocation(OLD), invocation(NEW)
        self.assertIn('out="$root/build/fix-20260914/timing"', OLD)
        self.assertIn('directory="$out/$mode-$module-v1"', OLD)
        self.assertIn('base="$root/build/fix-20260914/timing"', NEW)
        self.assertIn('label="formal-$module-v1"', NEW)
        self.assertEqual(before[before.index('--output-root') + 1], '$directory')
        self.assertEqual(after[after.index('--output-root') + 1], '$base/$label')
        for tokens in (before, after):
            tokens[tokens.index('--output-root') + 1] = 'same-formal-output-root'
        self.assertEqual(before[before.index('--timeout') + 1], '1800')
        self.assertEqual(after[after.index('--timeout') + 1], '3600')
        before[before.index('--timeout') + 1] = '3600'
        self.assertEqual(before, after)
        self.assertNotIn('--pilot', after)

    def test_only_five_not_started_coupled_modules_use_the_longer_wall_guard(self):
        selection = re.search(r'long_wall=false\s+case "\$module" in\s+([^\n]+)\) long_wall=true ;;', NEW).group(1).strip()
        self.assertEqual(set(selection.split('|')), {
            'coupled_be_nr_rkl2_all_transport',
            'coupled_bd_rkl1_all_transport', 'coupled_bd_rkl2_all_transport',
            'coupled_ros4_rkl1_all_transport', 'coupled_ros4_rkl2_all_transport',
        })
        self.assertIn('execution=(bash "$recipes/replay-fixed-timing-20260914.sh" formal "$module")', NEW)
        self.assertIn('wall=1800', NEW)

    def test_outer_resource_and_no_overwrite_gates_remain(self):
        for literal in ('timeout --signal=INT --kill-after=30s 24h',
                        '--min-available-mib 32768 --max-swap-growth-mib 64 --pressure-guard --gpu-memory-device 0',
                        'test ! -e "$base/$label"', 'flock -n 9'):
            self.assertIn(literal, NEW)
        self.assertNotIn('rm -', NEW)

    def test_original_completed_science_and_frozen_product_checks_remain(self):
        for source in (OLD, NEW):
            self.assertIn('len(r["cases"])==18 and len(r["lanes"])==36 and len(r["comparisons"])==18', source)
            self.assertIn('coupled-all-transport-restart tails', source)
            self.assertGreaterEqual(source.count('sha256sum -c build/fix-20260914/full-build-v1/artifacts.sha256'), 2)
            self.assertGreaterEqual(source.count('sha256sum -c build/corrected-fused-20260914/artifacts.sha256'), 2)


if __name__ == '__main__':
    unittest.main(verbosity=2)
