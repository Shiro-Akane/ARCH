"""Read-only byte/inventory checks for failed evidence, never a physics PASS."""
import argparse
import json
from pathlib import Path, PurePosixPath
import subprocess
import preserve_failed_formal as p


def verify(evidence, record, raw, compact):
    for path, key in ((raw, 'raw'), (compact, 'compact')):
        p.require(p.identity(path) == {k: record[key][k] for k in ('bytes', 'sha256')},
                  'Local archive bytes differ from server attestation')
    p.require(record['status'] == 'interrupted_attempt_preserved_not_qualified', 'Wrong archive status')
    manifest = json.loads((evidence / 'raw-manifest.json').read_text())
    mapping = json.loads((evidence / 'projection-map.json').read_text())
    omitted = json.loads((evidence / 'omissions.json').read_text())
    p.require(not (set(mapping.values()) & set(omitted)) and
              set(mapping.values()) | set(omitted) == set(manifest), 'Incomplete or overlapping projection')
    p.require(len(set(mapping.values())) == len(mapping), 'Duplicate projection originals')
    metadata = {'raw-manifest.json', 'projection-map.json', 'omissions.json',
                'failure-identity.json', 'failure-record.json', 'raw-archive.json'}
    p.require({v.relative_to(evidence).as_posix() for v in evidence.rglob('*') if v.is_file()}
              == set(mapping) | metadata, 'Missing or unexpected compact file')
    for relative, original in mapping.items():
        path = evidence / relative
        pure = PurePosixPath(relative)
        p.require(not pure.is_absolute() and '..' not in pure.parts and not path.is_symlink() and
                  path.resolve().is_relative_to(evidence.resolve()), 'Unsafe projection member')
        p.require(p.identity(path) == manifest[original], 'Projected evidence differs')
    for original, row in omitted.items():
        p.require({k: row[k] for k in ('bytes', 'sha256')} == manifest[original], 'Omission identity differs')
        path = PurePosixPath(original)
        p.require(path.suffix == '.h5' or (path.suffix == '.tsv' and row['bytes'] > 2 * 1024 * 1024),
                  'Unexpected omission class')
    members = subprocess.run(['tar', '-tf', str(raw)], capture_output=True, text=True, check=True).stdout.splitlines()
    p.require(all(not PurePosixPath(v).is_absolute() and '..' not in PurePosixPath(v).parts for v in members),
              'Unsafe raw archive member')
    p.require(set(manifest) <= set(members), 'Raw archive lacks manifest members')
    p.require(len(manifest) == record['raw']['files'], 'Raw manifest count differs')
    report = json.loads((evidence / 'evidence.json').read_text())
    failure = json.loads((evidence / 'failure-record.json').read_text())
    guards = [k for k, v in mapping.items() if v.endswith('/' + p.LABEL + '-guard.log')]
    p.require(len(guards) == 1, 'Ambiguous original guard log')
    p.validate_failure(report, (evidence / guards[0]).read_text(), failure['controller_exit'])
    p.require(not failure['scientific_validation_complete'] and not failure['release_qualified'] and
              not failure['samples_may_be_mixed_with_retry'], 'Failure incorrectly promoted')
    identity = json.loads((evidence / 'failure-identity.json').read_text())
    for version in ('baseline', 'candidate'):
        for section in ('artifacts', 'artifact_observation', 'source', 'build', 'execution_environment'):
            p.require(identity[version][section] == report['identities_before'][version][section],
                      'Source/build identity changed after failure')
    return dict(status='failed_attempt_backup_identity_verified', scientific_validation_complete=False,
        raw=p.identity(raw), compact=p.identity(compact), raw_manifest_files=len(manifest),
        projected_files=len(mapping), omitted_files=len(omitted),
        completed_runs=80, incomplete_runs=1, comparisons_recorded=77,
        samples_may_be_mixed_with_retry=False)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--archives', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    record = json.loads((args.root / 'archive.json').read_text())
    result = verify(args.root / 'evidence', record, args.archives / (p.NAME + '.tar.zst'),
                    args.archives / (p.NAME + '-compact.tar.zst'))
    p.save_new(args.output, result)
    print(json.dumps(result, indent=2))
