"""Preparation-only protocol for the next window-factory capacity regression.

This module neither builds nor launches anything. It reuses the frozen original
capacity validator and retains the 32-window/pool-8-or-32 scope: passing it does
not qualify multi-page ODEs, longer trajectories, Helm or application performance.
"""
import hashlib
import importlib.util
import json
import math
from pathlib import Path
import re

FOCUSED_SHA = 'f7f449b2ac96e053f496f777abf77e6400b1a562a0f0a348a58ae30d082c3e37'
METHODS = ('be_nr', 'bd', 'ros4')
POOLS = (8, 32)
MATRIX = tuple((n, method, pool) for n in (150, 200) for method in METHODS for pool in POOLS)
UNQUALIFIED = ('paged_ode_qualified', 'application_qualified', 'performance_qualified', 'release_qualified')


def focused_helpers():
    path = Path(__file__).with_name('run_focused.py')
    if hashlib.sha256(path.read_bytes()).hexdigest() != FOCUSED_SHA:
        raise ValueError('frozen focused helpers changed')
    spec = importlib.util.spec_from_file_location('capacity_focused_helpers', path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def parent_gate(root):
    """Require the real six-case result AND both byte-verified prior archives."""
    helpers = focused_helpers()
    parents = list(helpers.factory_gate(root))
    collection_path = root / 'window-focused-collection-v1.json'
    receipt_path = root / 'window-focused-local-receipt-v1.json'
    collection = json.loads(collection_path.read_text())
    receipt = json.loads(receipt_path.read_text())
    expected = sorted(f'audit{n}-{method}-pool2' for n in (150, 200) for method in METHODS)
    if (collection.get('profile') != 'focused' or collection.get('worker_exit_code') != 0
            or collection.get('trajectory_matrix_pass') is not True
            or collection.get('owners_verified') is not True
            or collection.get('completed_harnesses') != expected
            or collection.get('planned_harnesses') != expected
            or any(collection.get(key) is not False for key in UNQUALIFIED)
            or receipt.get('status') != 'both_archives_and_all_members_byte_verified'):
        raise ValueError('complete focused result with verified backup required')
    for kind in ('raw', 'compact'):
        entry = collection[kind]
        path = Path(entry['path'])
        if (receipt.get(kind) != entry or path.is_symlink() or not path.is_file()
                or path.stat().st_size != entry['bytes'] or helpers.sha(path) != entry['sha256']):
            raise ValueError('focused predecessor archive identity changed')
    return (*parents, collection_path, receipt_path)


def parse_owner(text, pool):
    if pool not in POOLS:
        raise ValueError('unregistered capacity pool')
    lines = [line for line in text.splitlines() if line.startswith('WINDOW_OWNER')]
    match = re.fullmatch(r'WINDOW_OWNER selected_window=(\d+) actual_capacity=(\d+) native_capacity=(\d+) '
                         r'workspace_bytes=(\d+) workspace_budget=(\d+) device_warp=(\d+)',
                         lines[0] if len(lines) == 1 else '')
    if not match:
        raise ValueError('one actual window owner declaration required')
    selected, capacity, native, size, budget, warp = map(int, match.groups())
    aggregate = [line.split(',') for line in text.splitlines() if line.startswith('metrics,')]
    if (len(aggregate) != 1 or len(aggregate[0]) != 9 or selected != 32
            or capacity != pool or native != pool or budget != 32 * 1024 * 1024
            or warp != 32 or not 0 < size <= budget or aggregate[0][7] != str(pool)
            or size != pool * int(aggregate[0][8])):
        raise ValueError('actual owner capacity or workspace budget changed')
    return dict(selected_window=selected, actual_capacity=capacity, native_capacity=native,
                workspace_bytes=size, workspace_budget=budget, device_warp=warp)


def validate_transcript(record, output, factory, exit_code):
    helpers = focused_helpers()
    original = helpers.load_validator()
    if (record.get('profile') != 'capacity' or record.get('selected_window') != 32
            or record.get('original_harness_sha256') != helpers.HARNESS_SHA
            or record.get('original_validator_sha256') != helpers.VALIDATOR_SHA
            or any(record.get(key) is not False for key in UNQUALIFIED)):
        raise ValueError('original capacity profile and limited scope required')
    expected = [f'audit{n}-{method}-pool{pool}' for n, method, pool in MATRIX]
    commands = record.get('commands', [])
    names = [row['name'] for row in commands]
    if names != expected[:len(names)] or (exit_code == 0 and names != expected):
        raise ValueError('missing, duplicated or reordered capacity commands')
    for row in commands:
        network, method, pool_name = row['name'].split('-')
        pool = int(pool_name[4:])
        command = [str(factory / ('arch_cuda_generated_sparse_burn_' + network)),
                   *original.trajectory_args('capacity', method, pool)]
        if (row.get('command') != command or row.get('cwd') != str(factory)
                or row.get('environment') != {'ARCH_NATIVE_WINDOW_CELLS': '32'}
                or row.get('timeout_seconds') != original.PROFILES['capacity']['wall']):
            raise ValueError('unexpected factory, capacity controls or environment')
        if row.get('status') == 'passed' and (row.get('returncode') != 0
                or row.get('timed_out') is not False
                or not isinstance(row.get('elapsed_seconds'), (int, float))
                or not math.isfinite(row['elapsed_seconds']) or row['elapsed_seconds'] < 0):
            raise ValueError('inconsistent successful capacity command')
    result = original.validate('capacity', record, output, exit_code)
    owners = {name: parse_owner((output / (name + '.stdout')).read_text(), int(name.rsplit('pool', 1)[1]))
              for name in result['completed_harnesses']}
    if record.get('owners') != owners:
        raise ValueError('owner record differs from capacity transcripts')
    return dict(**result, paged_ode_qualified=False, owners_verified=True)
