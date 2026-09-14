"""Read-only audit of completed formal reports and their local archive projections.

This rechecks evidence and storage integrity, not a new independent physics oracle.
No live run, source tree, original report or archive is modified.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path, PurePosixPath


def require(condition, message):
    if not condition:
        raise ValueError(message)


def read(path):
    return json.loads(path.read_text(encoding='utf-8'))


def sha(path):
    h = hashlib.sha256()
    with path.open('rb') as stream:
        while chunk := stream.read(1024 * 1024):
            h.update(chunk)
    return h.hexdigest()


def verify_projection(root, manifest, mapping, omitted):
    root = root.resolve(strict=True)
    for relative, entry in omitted.items():
        parts = PurePosixPath(relative)
        require(not parts.is_absolute() and '..' not in parts.parts and '\\' not in relative,
                f'unsafe omission path: {relative}')
        original = entry['original_path']
        require(original in manifest, f'omission absent from raw manifest: {relative}')
        expected = manifest[original]
        require(entry['bytes'] == expected['bytes'] and entry['sha256'] == expected['sha256'],
                f'omission identity mismatch: {relative}')
        require(PurePosixPath(relative).suffix == '.tsv' and entry['bytes'] > 2 * 1024 * 1024,
                f'unexpected compact omission: {relative}')
    present = 0
    for relative, original in mapping.items():
        parts = PurePosixPath(relative)
        require(not parts.is_absolute() and '..' not in parts.parts and '\\' not in relative,
                f'unsafe projection path: {relative}')
        target = root.joinpath(*parts.parts)
        require(original in manifest, f'projection absent from raw manifest: {relative}')
        if not target.exists():
            require(relative in omitted and omitted[relative]['original_path'] == original,
                    f'unexplained missing projected file: {relative}')
            continue
        require(not target.is_symlink() and target.resolve().is_relative_to(root),
                f'projection escapes directory: {relative}')
        expected = manifest[original]
        require(target.stat().st_size == expected['bytes'] and sha(target) == expected['sha256'],
                f'projected bytes differ: {relative}')
        present += 1
    return present


def verify_receipt(receipt, raw, manifest, trace):
    require(receipt['applied'] is True, 'postprocessing did not complete')
    require(receipt['archive_sha256'] == receipt['windows_copy_sha256'] == raw['sha256'],
            'postprocessing archive SHA mismatch')
    require(receipt['windows_copy_bytes'] == raw['bytes'], 'postprocessing archive size mismatch')
    verified = receipt['verified']
    require(len(receipt['removed']) == len(set(receipt['removed'])) and
            set(receipt['removed']) == set(verified), 'incomplete postprocessing ledger')
    require(receipt['bytes'] == sum(v['bytes'] for v in verified.values()), 'ledger total differs')
    for original, identity in verified.items():
        require(identity == manifest[original], 'postprocessing identity differs from raw manifest')
    if trace:
        entries = receipt['compressed']
        require(len(entries) == len(verified) and {v['original'] for v in entries} == set(verified),
                'lossless compression ledger incomplete')
        require(all(v['decompressed_sha256'] == verified[v['original']]['sha256'] for v in entries),
                'decompressed trace identity mismatch')


def audit_module(directory, archives, module, gate):
    base = directory / module
    evidence = base / 'evidence'
    report = read(evidence / 'evidence.json')
    gate.check_report(report, False)
    require({v['id'] for v in report['cases']} == {f'{module}_b{n}' for n in (8, 32, 128)},
            'wrong module/scale coverage')
    require('observer' not in report, 'instrumented report cannot qualify formal samples')
    for version in ('baseline', 'candidate'):
        for section in ('artifacts', 'artifact_observation', 'source', 'build', 'execution_environment'):
            require(report['identities_before'][version][section] == report['identities_after'][version][section],
                    f'changed {version} {section}')
    raw = read(evidence / 'raw-archive.json')
    require(raw['status'] == 'passed' and raw['module'] == module and
            raw['runs'] == 108 and raw['comparisons'] == 105, 'archive qualification mismatch')
    local = archives / PurePosixPath(raw['archive']).name
    require(local.stat().st_size == raw['bytes'] and sha(local) == raw['sha256'],
            'local raw archive differs from verified server record')
    manifest = read(evidence / 'raw-manifest.json')
    require(len(manifest) == raw['files'], 'raw file count differs')
    omitted = {}
    for name in ('omitted-large-files.json', 'git-projection-omissions.json'):
        path = evidence / name
        if path.exists():
            record = read(path)
            require('files' in record or 'omitted' in record, 'unrecognized omission format')
            entries = record.get('files', record.get('omitted'))
            require(isinstance(entries, dict), 'invalid omission format')
            require(not (omitted.keys() & entries.keys()), 'duplicate omissions')
            omitted.update(entries)
    present = verify_projection(evidence, manifest, read(evidence / 'projection-map.json'), omitted)
    for name, trace in (('hdf-compaction.json', False), ('trace-compression.json', True)):
        verify_receipt(read(base / name), raw, manifest, trace)
    return dict(module=module, runs=108, comparisons=105, archive_sha256=raw['sha256'],
                archive_bytes=raw['bytes'], verified_projected_files=present,
                documented_large_tsv_omissions=len(omitted), status='passed')


def main():
    spec = importlib.util.spec_from_file_location('formal_gate',
        Path(__file__).with_name('archive-formal-timing-v2-20260914.py'))
    gate = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(gate)
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('directory', type=Path)
    p.add_argument('--archives', type=Path, required=True)
    p.add_argument('--modules', nargs='+', choices=gate.MODULES)
    p.add_argument('--output', type=Path, help='new audit artifact; never overwrite an existing record')
    a = p.parse_args()
    modules = a.modules or gate.MODULES
    require(len(set(modules)) == len(modules), 'duplicate modules')
    rows = [audit_module(a.directory, a.archives, m, gate) for m in modules]
    output = json.dumps(dict(status='passed', modules=len(rows), complete_11_module_coverage=len(rows) == 11,
        runs=108*len(rows), comparisons=105*len(rows), records=rows,
        scope='read-only report and local backup integrity audit; not new physics or sanitizer validation'), indent=2)
    if a.output:
        with a.output.open('x', encoding='utf-8', newline='\n') as stream:
            stream.write(output + '\n')
    print(output)


if __name__ == '__main__':
    main()
