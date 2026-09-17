#!/usr/bin/env python3
"""Catalog old Git references and identify objects absent from published history."""
import gzip
import json
import os
from pathlib import Path
import subprocess
from mainline_retirement_inventory import OLD, OUT, save


def git(root, *args):
    return subprocess.check_output(['git', '-C', str(root), *args], text=True,
                                   env={**os.environ, 'GIT_OPTIONAL_LOCKS': '0'})


def main():
    published = set(json.loads(Path('/tmp/mainline-published-git-index-20260918.json').read_text())['objects'])
    repositories = json.loads((OUT / 'repositories.json').read_text())
    results, objects = [], {}
    for repo in repositories:
        if repo['dependency']:
            continue
        root = Path(repo['path'])
        ids = {line.split(' ', 1)[0] for line in git(root, 'rev-list', '--objects', '--all', '--reflog', 'HEAD').splitlines()}
        missing = sorted(ids - published)
        refs = git(root, 'for-each-ref', '--format=%(objectname) %(refname)').splitlines()
        results.append({**repo, 'refs': refs, 'objects': len(ids), 'missing_count': len(missing),
                        'git_common_dir': git(root, 'rev-parse', '--path-format=absolute', '--git-common-dir').strip()})
        for key in missing:
            objects.setdefault(key, str(root))
    bundles = []
    with gzip.open(OUT / 'inventory.jsonl.gz', 'rt') as stream:
        for line in stream:
            entry = json.loads(line)
            if entry['category'] != 'archive_container' or not entry['path'].endswith('.bundle'):
                continue
            path = OLD / entry['path']
            refs = git(OLD, 'bundle', 'list-heads', str(path)).splitlines()
            absent = [r for r in refs if r.split(' ', 1)[0] not in published]
            bundles.append({'path': str(path), 'refs': refs, 'unpublished_heads': absent})
    save(OUT / 'git-history-coverage.json', {'repositories': results, 'unique_unpublished_objects': len(objects),
                                         'bundles': bundles})
    save(OUT / 'git-unpublished-objects.json', objects)
    print(json.dumps({'project_repositories': len(results), 'unique_unpublished_objects': len(objects),
                      'bundle_count': len(bundles), 'unpublished_bundle_heads': sum(len(e['unpublished_heads']) for e in bundles)}, indent=2))


if __name__ == '__main__':
    main()
