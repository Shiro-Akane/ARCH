"""Freeze a local-only capacity payload; never connects to or dispatches a server."""
import argparse
import hashlib
import io
import json
from pathlib import Path
import tarfile

from capacity_protocol import MATRIX, FOCUSED_SHA, focused_helpers


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    if args.output.exists() or args.output.is_symlink():
        raise ValueError('new preparation output required')
    here = Path(__file__).resolve().parent
    original = focused_helpers()
    sources = {name: here / name for name in ('run_capacity.py', 'capacity_protocol.py',
        'run_focused.py', 'capacity-worker.sh', 'capacity-dispatch.sh', 'collect_capacity.py')}
    sources['trajectory_validation.py'] = here.parent / 'batched-kernels/run_trajectories.py'
    contents = {name: path.read_bytes() for name, path in sources.items()}
    assert hashlib.sha256(contents['run_focused.py']).hexdigest() == FOCUSED_SHA
    assert hashlib.sha256(contents['trajectory_validation.py']).hexdigest() == original.VALIDATOR_SHA
    for name, data in contents.items():
        if name.endswith('.py'):
            compile(data, name, 'exec')
    args.output.mkdir(parents=True)
    archive = args.output / 'window-capacity-input-v1.tar'
    with tarfile.open(archive, 'x', format=tarfile.USTAR_FORMAT) as tar:
        for name, data in sorted(contents.items()):
            member = tarfile.TarInfo('window-capacity-input-v1/' + name)
            member.size = len(data)
            member.mode = 0o644
            tar.addfile(member, io.BytesIO(data))
    record = dict(status='prepared_locally_not_uploaded_or_executed',
        archive=str(archive), bytes=archive.stat().st_size,
        sha256=hashlib.sha256(archive.read_bytes()).hexdigest(),
        files={name: dict(bytes=len(data), sha256=hashlib.sha256(data).hexdigest())
               for name, data in sorted(contents.items())},
        profile='capacity', matrix=[list(row) for row in MATRIX],
        selected_window=32, storage_cells=[32, 33], pools=[8, 32],
        steps=4, duration=1e-10, per_harness_wall_seconds=7200,
        predecessor='window-focused-collection-v1.json plus matching local byte-verification receipt',
        scientific_settings_modified=False, requires_focused_backup_first=True,
        gpu_executed=False, paged_ode_qualified=False, application_qualified=False,
        performance_qualified=False, release_qualified=False)
    (args.output / 'prepared-capacity-input-v1.json').write_text(json.dumps(record, indent=2) + '\n')
    print(json.dumps(record, indent=2))


if __name__ == '__main__':
    main()
