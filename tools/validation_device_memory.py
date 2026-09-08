"""Summarize process-local CUDA allocation events from an Nsight SQLite export.

This reads allocator requests, not physical residency, driver/context overhead
or whole-device VRAM. Pool reserves are reported separately because they overlap
the requested allocations. Keep raw profiler files local: they can contain
environment/process metadata unrelated to the selected validation workload.
"""
import argparse
import json
from pathlib import Path
import sqlite3


def summarize_allocations(events):
    live, groups = {}, {}
    last_time = -1
    for event in events:
        time = event['time_ns']
        if time < last_time or event['bytes'] <= 0 or event['address'] <= 0:
            raise ValueError('invalid or unordered CUDA allocation event')
        last_time = time
        group_key = (event['global_pid'], event['device'])
        address_key = (*group_key, event['context'], event['address'])
        group = groups.setdefault(group_key, dict(
            global_pid=group_key[0], device=group_key[1], events=0,
            allocations=0, deallocations=0, largest_allocation_bytes=0,
            live_bytes_by_kind={}, peak_bytes_by_kind={},
            peak_device_requested_bytes=0, peak_device_time_ns=0,
            pool_reserved_peak_bytes={}))
        kind, size = event['kind'], event['bytes']
        if kind not in {'pageable', 'pinned', 'device', 'array', 'managed',
                        'device_static', 'managed_static'}:
            raise ValueError('unknown CUDA allocation memory kind')
        if event['operation'] == 'allocate':
            if address_key in live:
                raise ValueError('CUDA address allocated twice without release')
            live[address_key] = (kind, size)
            delta = size
            group['allocations'] += 1
            group['largest_allocation_bytes'] = max(group['largest_allocation_bytes'], size)
        elif event['operation'] == 'free':
            if live.pop(address_key, None) != (kind, size):
                raise ValueError('CUDA release has no matching allocation or extent')
            delta = -size
            group['deallocations'] += 1
        else:
            raise ValueError('unknown CUDA allocation operation')
        group['events'] += 1
        current = group['live_bytes_by_kind']
        current[kind] = current.get(kind, 0) + delta
        peaks = group['peak_bytes_by_kind']
        peaks[kind] = max(peaks.get(kind, 0), current[kind])
        device_bytes = sum(current.get(name, 0) for name in ('device', 'array', 'device_static'))
        if device_bytes > group['peak_device_requested_bytes']:
            group['peak_device_requested_bytes'] = device_bytes
            group['peak_device_time_ns'] = time
        pool = event.get('pool_address')
        if pool:
            reserve = event.get('pool_reserved_bytes')
            if reserve is None or reserve < 0:
                raise ValueError('CUDA pool event lacks its reserved extent')
            key = f"{event['context']}:{pool}"
            pools = group['pool_reserved_peak_bytes']
            pools[key] = max(pools.get(key, 0), reserve)
    if not groups:
        raise ValueError('CUDA memory trace is empty')
    return dict(scope='process-local CUDA allocator requests',
        physical_vram_measurement=False, all_allocations_released=not live,
        unmatched_live_allocations=len(live),
        note='Pool reserves overlap requested bytes. Driver/context overhead and managed-memory residency are not inferred.',
        processes=[groups[key] for key in sorted(groups)])


def read_profile(path):
    with sqlite3.connect(Path(path).resolve().as_uri() + '?mode=ro', uri=True) as database:
        kinds = dict(database.execute('SELECT id, name FROM ENUM_CUDA_MEM_KIND'))
        operations = dict(database.execute('SELECT id, name FROM ENUM_CUDA_DEV_MEM_EVENT_OPER'))
        operation_names = {'CUDA_DEV_MEM_EVENT_OPR_ALLOCATION': 'allocate',
                           'CUDA_DEV_MEM_EVENT_OPR_DEALLOCATION': 'free'}
        rows = database.execute('''SELECT start, globalPid, deviceId, contextId, address,
            bytes, memKind, memoryOperationType, localMemoryPoolAddress, localMemoryPoolSize
            FROM CUDA_GPU_MEMORY_USAGE_EVENTS ORDER BY start, rowid''')
        def events():
            for time, pid, device, context, address, size, kind, operation, pool, reserve in rows:
                if kind not in kinds or operation not in operations \
                        or operations[operation] not in operation_names \
                        or not kinds[kind].startswith('CUDA_MEMOPR_MEMORY_KIND_'):
                    raise ValueError('unrecognized Nsight CUDA allocation enum')
                yield dict(time_ns=time, global_pid=pid, device=device, context=context,
                    address=address, bytes=size,
                    kind=kinds[kind].removeprefix('CUDA_MEMOPR_MEMORY_KIND_').lower(),
                    operation=operation_names[operations[operation]],
                    pool_address=pool, pool_reserved_bytes=reserve)
        result = summarize_allocations(events())
        # Only retain names for processes that actually supplied CUDA events;
        # do not export the profiler's unrelated process/environment snapshot.
        for process in result['processes']:
            identity = database.execute('SELECT pid, name FROM PROCESSES WHERE globalPid=?',
                                        (process['global_pid'],)).fetchall()
            if len(identity) != 1:
                raise ValueError('CUDA allocation process identity is ambiguous')
            process.update(pid=identity[0][0], executable=identity[0][1])
        return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('profile', type=Path)
    arguments = parser.parse_args()
    print(json.dumps(read_profile(arguments.profile), indent=2, sort_keys=True))
