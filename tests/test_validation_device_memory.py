import importlib.util
from pathlib import Path
import sqlite3
import tempfile
import unittest

SPEC = importlib.util.spec_from_file_location('validation_device_memory',
    Path(__file__).resolve().parents[1] / 'tools/validation_device_memory.py')
memory = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(memory)


def event(time, address, size, operation, **extra):
    return dict(time_ns=time, global_pid=1, device=0, context=1,
                address=address, bytes=size, kind='device', operation=operation, **extra)


class DeviceMemoryTest(unittest.TestCase):
    def test_sqlite_enums_and_selected_process_identity(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'profile.sqlite'
            with sqlite3.connect(path) as database:
                database.executescript('''
                    CREATE TABLE ENUM_CUDA_MEM_KIND(id, name);
                    INSERT INTO ENUM_CUDA_MEM_KIND VALUES (73, 'CUDA_MEMOPR_MEMORY_KIND_DEVICE');
                    CREATE TABLE ENUM_CUDA_DEV_MEM_EVENT_OPER(id, name);
                    INSERT INTO ENUM_CUDA_DEV_MEM_EVENT_OPER VALUES (14, 'CUDA_DEV_MEM_EVENT_OPR_ALLOCATION');
                    INSERT INTO ENUM_CUDA_DEV_MEM_EVENT_OPER VALUES (15, 'CUDA_DEV_MEM_EVENT_OPR_DEALLOCATION');
                    CREATE TABLE PROCESSES(globalPid, pid, name);
                    INSERT INTO PROCESSES VALUES (1, 101, '/build/bin/ARCH');
                    INSERT INTO PROCESSES VALUES (2, 102, 'unrelated-process');
                    CREATE TABLE CUDA_GPU_MEMORY_USAGE_EVENTS(start, globalPid, deviceId,
                        contextId, address, bytes, memKind, memoryOperationType,
                        localMemoryPoolAddress, localMemoryPoolSize);
                    INSERT INTO CUDA_GPU_MEMORY_USAGE_EVENTS VALUES (1, 1, 0, 1, 10, 100, 73, 14, NULL, NULL);
                    INSERT INTO CUDA_GPU_MEMORY_USAGE_EVENTS VALUES (2, 1, 0, 1, 10, 100, 73, 15, NULL, NULL);
                ''')
            result = memory.read_profile(path)
            self.assertTrue(result['all_allocations_released'])
            process, = result['processes']
            self.assertEqual(process['pid'], 101)
            self.assertEqual(process['peak_device_requested_bytes'], 100)
            self.assertNotIn('unrelated-process', str(result))

    def test_old_new_overlap_and_reuse(self):
        events = [event(1, 10, 100, 'allocate'), event(2, 20, 200, 'allocate'),
                  event(3, 30, 50, 'allocate'), event(4, 30, 50, 'free'),
                  event(5, 10, 100, 'free'), event(6, 10, 50, 'allocate'),
                  event(7, 10, 50, 'free'), event(8, 20, 200, 'free')]
        result = memory.summarize_allocations(events)
        self.assertTrue(result['all_allocations_released'])
        process, = result['processes']
        self.assertEqual(process['peak_device_requested_bytes'], 350)
        self.assertEqual(process['peak_device_time_ns'], 3)
        self.assertEqual(process['allocations'], 4)

    def test_process_and_pool_are_separate(self):
        first = event(1, 10, 100, 'allocate', pool_address=1000, pool_reserved_bytes=500)
        second = dict(event(2, 10, 200, 'allocate'), global_pid=2)
        result = memory.summarize_allocations([first, second])
        self.assertFalse(result['all_allocations_released'])
        self.assertEqual([p['peak_device_requested_bytes'] for p in result['processes']], [100, 200])
        self.assertEqual(result['processes'][0]['pool_reserved_peak_bytes'], {'1:1000': 500})

    def test_pinned_and_managed_are_not_device_residency(self):
        events = [dict(event(1, 10, 100, 'allocate'), kind='pinned'),
                  dict(event(2, 20, 200, 'allocate'), kind='managed')]
        process, = memory.summarize_allocations(events)['processes']
        self.assertEqual(process['peak_device_requested_bytes'], 0)
        self.assertEqual(process['peak_bytes_by_kind'], {'pinned': 100, 'managed': 200})

    def test_incomplete_or_corrupt_trace_fails(self):
        allocation = event(1, 10, 100, 'allocate')
        for events in ([], [event(1, 10, 100, 'free')], [allocation, allocation],
                       [allocation, event(2, 10, 99, 'free')],
                       [event(2, 10, 100, 'allocate'), event(1, 10, 100, 'free')],
                       [dict(allocation, kind='unknown')], [dict(allocation, operation='unknown')],
                       [dict(allocation, bytes=0)], [dict(allocation, pool_address=1000)]):
            with self.subTest(events=events), self.assertRaises(ValueError):
                memory.summarize_allocations(events)


if __name__ == '__main__':
    unittest.main()
