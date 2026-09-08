"""Pure-data controls for this result recipe; no CUDA process is started."""
from copy import deepcopy
import importlib.util
import json
from pathlib import Path
import unittest

SPEC = importlib.util.spec_from_file_location('capacity_replay', Path(__file__).with_name('replay.py'))
replay = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(replay)
from validation_device_memory import summarize_allocations


def event(time, address, kind='device', operation='allocate', pid=1, size=29):
    return dict(time_ns=time, global_pid=pid, device=0, context=1,
                address=address, bytes=size, kind=kind, operation=operation)


def closed(kind='device'):
    return [event(1, 10, kind), event(2, 10, kind, 'free')]


class AllocationLifetimesTest(unittest.TestCase):
    def classify(self, events):
        return replay.check_allocation_lifetimes(summarize_allocations(events))

    def test_closed_device(self):
        self.assertTrue(self.classify(closed())['all_allocations_released'])

    def test_closed_array(self):
        self.assertTrue(self.classify(closed('array'))['dynamic_device_allocation_coverage'])

    def test_device_static_residual_retained(self):
        result = self.classify(closed() + [event(3, 20, 'device_static')])
        self.assertTrue(result['all_dynamic_allocations_released'])
        self.assertFalse(result['all_allocations_released'])
        self.assertEqual(result['processes'][0]['static_live_bytes_by_kind'], {'device_static': 29})

    def test_managed_static_residual_retained(self):
        result = self.classify(closed() + [event(3, 20, 'managed_static')])
        self.assertFalse(result['all_allocations_released'])
        self.assertEqual(result['processes'][0]['static_live_bytes_by_kind'], {'managed_static': 29})

    def test_static_release_record_honored(self):
        result = self.classify(closed() + [event(3, 20, 'device_static'),
                                         event(4, 20, 'device_static', 'free')])
        self.assertTrue(result['all_allocations_released'])

    def test_every_dynamic_kind_leak_rejected(self):
        for kind in ('device', 'array', 'managed', 'pageable', 'pinned'):
            with self.subTest(kind=kind), self.assertRaises(ValueError):
                self.classify(closed() + [event(3, 20, 'device_static'), event(4, 30, kind)])

    def test_second_process_leak_rejected(self):
        with self.assertRaises(ValueError):
            self.classify(closed() + [event(3, 10, pid=2)])

    def test_static_only_is_not_dynamic_coverage(self):
        for kind in ('device_static', 'managed_static'):
            with self.subTest(kind=kind), self.assertRaises(ValueError):
                self.classify([event(1, 10, kind)])

    def test_host_or_managed_only_is_not_device_coverage(self):
        for kind in ('pageable', 'pinned', 'managed'):
            with self.subTest(kind=kind), self.assertRaises(ValueError):
                self.classify(closed(kind))

    def test_missing_process_coverage_rejected(self):
        for value in (None, {}, {'processes': []}):
            with self.subTest(value=value), self.assertRaises(ValueError):
                replay.check_allocation_lifetimes(value)

    def test_empty_byte_maps_rejected(self):
        value = summarize_allocations(closed())
        value['processes'][0].update(live_bytes_by_kind={}, peak_bytes_by_kind={})
        with self.assertRaises(ValueError):
            replay.check_allocation_lifetimes(value)

    def test_unknown_kind_rejected(self):
        value = summarize_allocations(closed())
        for mapping in ('live_bytes_by_kind', 'peak_bytes_by_kind'):
            value['processes'][0][mapping]['unknown'] = 0
        with self.assertRaises(ValueError):
            replay.check_allocation_lifetimes(value)

    def test_mismatched_peak_kinds_rejected(self):
        value = summarize_allocations(closed())
        value['processes'][0]['peak_bytes_by_kind']['device_static'] = 29
        with self.assertRaises(ValueError):
            replay.check_allocation_lifetimes(value)

    def test_invalid_live_byte_totals_rejected(self):
        for invalid in (-1, True, 0.0, '0'):
            value = summarize_allocations(closed())
            value['processes'][0]['live_bytes_by_kind']['device'] = invalid
            with self.subTest(value=invalid), self.assertRaises(ValueError):
                replay.check_allocation_lifetimes(value)

    def test_invalid_peak_byte_totals_rejected(self):
        for invalid in (-1, True, 29.0, '29'):
            value = summarize_allocations(closed())
            value['processes'][0]['peak_bytes_by_kind']['device'] = invalid
            with self.subTest(value=invalid), self.assertRaises(ValueError):
                replay.check_allocation_lifetimes(value)

    def test_peak_below_live_rejected(self):
        value = summarize_allocations(closed() + [event(3, 20, 'device_static')])
        value['processes'][0]['peak_bytes_by_kind']['device_static'] = 1
        with self.assertRaises(ValueError):
            replay.check_allocation_lifetimes(value)

    def test_inconsistent_release_status_rejected(self):
        for key, invalid in (('all_allocations_released', False),
                             ('all_allocations_released', 1),
                             ('unmatched_live_allocations', -1),
                             ('unmatched_live_allocations', True),
                             ('unmatched_live_allocations', 1)):
            value = summarize_allocations(closed())
            value[key] = invalid
            with self.subTest(key=key, value=invalid), self.assertRaises(ValueError):
                replay.check_allocation_lifetimes(value)

    def test_raw_summary_is_not_modified(self):
        value = summarize_allocations(closed() + [event(3, 20, 'device_static')])
        before = deepcopy(value)
        replay.check_allocation_lifetimes(value)
        self.assertEqual(value, before)

    def test_json_round_trip_reclassification(self):
        value = summarize_allocations(closed() + [event(3, 20, 'managed_static', size=103)])
        self.assertEqual(replay.check_allocation_lifetimes(value),
                         replay.check_allocation_lifetimes(json.loads(json.dumps(value))))


if __name__ == '__main__':
    unittest.main()
