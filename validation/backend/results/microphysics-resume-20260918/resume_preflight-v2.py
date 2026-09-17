"""Read-only checks for the frozen window factory after dependency migration.

This program never runs a CUDA executable, allocates a device context or starts
a worker. GPU activity is reported as blocked readiness, not a numerical failure.
"""
import argparse
import datetime
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import subprocess


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--root', required=True, type=Path)
    parser.add_argument('--focused-script', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    root = args.root
    assert root == Path('/home/ubuntu/projects/ARCH-native-wave-v4-20260916')
    assert not args.output.exists()
    report = dict(utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
                  status='checking', gpu_executed=False, numerical_qualified=False,
                  performance_qualified=False, checks={})
    try:
        frozen_sha = 'f7f449b2ac96e053f496f777abf77e6400b1a562a0f0a348a58ae30d082c3e37'
        actual_sha = hashlib.sha256(args.focused_script.read_bytes()).hexdigest()
        if actual_sha != frozen_sha:
            raise ValueError('Frozen focused input v2 changed; no imported code executed')
        report['focused_script_sha256'] = actual_sha
        focused = load('frozen_window_focused', args.focused_script)
        helper = load('existing_factory_helpers', root / 'input/collect_factory.py')
        parents = focused.factory_gate(root)
        built = focused.verify_factory_identity(root / 'window-factory-v1')
        report['checks']['factory_and_archive_identity'] = 'passed'
        report['factory_artifacts'] = built['artifacts']
        report['prior_receipts'] = {str(p): focused.sha(p) for p in parents}
        manifests = {}
        for name in ('source-files', 'network-files', 'vendor', 'artifacts'):
            path = root / 'factory-control-v2' / (name + '.sha256')
            entries = helper.inventory(path, root / 'source')
            manifests[name] = dict(sha256=focused.sha(path), files_verified=len(entries))
        report['checks']['original_source_network_vendor_artifacts'] = manifests
        pending = [name for name in ('window-focused-input-v2.tar', 'window-focused-input-v1',
                                     'window-focused-control-v1', 'window-focused-v1')
                   if (root / name).exists() or (root / name).is_symlink()]
        report['existing_focused_paths'] = pending
        processes = subprocess.run(['nvidia-smi', '--query-compute-apps=pid,process_name,used_memory',
                                    '--format=csv,noheader'], capture_output=True, text=True, check=True)
        report['gpu_processes'] = processes.stdout.strip().splitlines()
        report['gpu_snapshot'] = subprocess.check_output(['nvidia-smi',
            '--query-gpu=name,uuid,driver_version,utilization.gpu,memory.used,memory.total',
            '--format=csv'], text=True)
        active = []
        for name in ('ARCH', 'nvcc', 'ptxas', 'cc1plus',
                     'arch_cuda_generated_sparse_burn_audit150', 'arch_cuda_generated_sparse_burn_audit200'):
            result = subprocess.run(['pgrep', '-f', '^.*/' + re.escape(name) + r'( |$)'],
                                    capture_output=True, text=True)
            assert result.returncode in (0, 1)
            if result.returncode == 0:
                active.append(dict(name=name, pids=result.stdout.split()))
        report['active_compute_or_build'] = active
        report['static_preflight_pass'] = True
        report['gpu_dispatch_ready'] = not report['gpu_processes'] and not pending and not active
        report['status'] = 'ready_for_original_dispatch' if report['gpu_dispatch_ready'] else 'dispatch_blocked'
    except BaseException as error:
        report.update(status='identity_or_preflight_failed', static_preflight_pass=False,
                      gpu_dispatch_ready=False, error=repr(error))
        raise
    finally:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        with args.output.open('x') as stream:
            json.dump(report, stream, indent=2)
            stream.write('\n')
        print(json.dumps(report, indent=2), flush=True)


if __name__ == '__main__':
    main()
