#!/usr/bin/env python3
"""Record and review Git's pre-deletion index/LFS metadata refresh, without hiding it.

Does not modify Git or weaken the cleanup script's strict original-snapshot check.
"""
import argparse
import json
from pathlib import Path
import subprocess

from server_storage_audit import digest, save
from server_storage_prepare import record


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--audit', type=Path, required=True)
    parser.add_argument('--after', action='store_true')
    args = parser.parse_args()
    baseline_path = args.audit / 'git-metadata-before-deletion.json'
    if not args.after:
        assert not baseline_path.exists()
        check = json.loads((args.audit / 'retained-precheck.json').read_text())
        assert not check['missing']
        sources = json.loads((args.audit / 'source-repo-status.json').read_text())
        records = []
        for name in check['changed']:
            p = Path(name)
            assert p.name in {'config', 'index'} and p.parent.name == '.git', name
            source = str(p.parent.parent)
            head = subprocess.check_output(['git', '-C', source, 'rev-parse', 'HEAD'], text=True).strip()
            assert head == sources[source]['head'], source
            records.append(dict(record(p), source=source, head=head))
        save(baseline_path, {'files': records, 'meaning': 'Already changed before deletion, after Git status/LFS inspection. '
                             'Original snapshot is retained; these paths are not deletion candidates.'})
        print(f'Recorded {len(records)} pre-existing Git metadata refreshes; repository HEADs unchanged.')
    else:
        before = json.loads(baseline_path.read_text())
        after = json.loads((args.audit / 'cleanup-receipt.json').read_text())
        retained = after['retained_file_check']
        assert not retained['missing']
        assert set(retained['changed']) == {r['path'] for r in before['files']}
        for r in before['files']:
            assert digest(r['path']) == r['sha256'], r['path']
            head = subprocess.check_output(['git', '-C', r['source'], 'rev-parse', 'HEAD'], text=True).strip()
            assert head == r['head'], r['source']
        assert not after['protected_hash_failures']
        result = {'all_passed_after_review': True, 'missing_retained_files': 0,
                  'unexpected_retained_changes': 0,
                  'preexisting_git_metadata_refreshes_unchanged_by_deletion': len(before['files']),
                  'protected_hash_count': after['protected_hash_count'],
                  'strict_receipt': 'cleanup-receipt.json',
                  'note': 'Original strict check reports pre-deletion Git metadata changes; its result is not overwritten.'}
        save(args.audit / 'post-cleanup-review.json', result)
        print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
