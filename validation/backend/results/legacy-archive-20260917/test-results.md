# Archive tooling checks

Run date: 2026-09-17 (Asia/Shanghai).

## Local archive unit tests

Command: `python tools/test_legacy_archive_pack.py`.
Result: 4/4 passed. Raw output: `archive-unit-tests.stdout`.

Coverage: pack corruption detection, traversal-origin rejection, unknown-source
classification, duplicate chunk reuse, restoration and refusal to overwrite.

## Linux retirement selection tests

Command on the server: `cd /tmp && PYTHONDONTWRITEBYTECODE=1 python3 test_retire_failed_source.py`.
Result: 5/5 passed, process exit code 0. These are read-only predicate tests and
do not call the deletion entry point or create/remove production files.

Tests: reviewed source subtree selection; root build configuration selection;
logs/data/docs/scripts preserved; unrelated mutation/current source trees rejected;
nested build configuration preserved.

Actual deletion checks are independently recorded in
`failed-source-retirement/source-cleanup-receipt.json`: 199 source/config files
deleted and 1,130 retained files verified. This result does not rely on simulated
selection tests alone.

## Real archive restoration

`restore-failure-log.json`, `restore-successor-log.json`, and
`restore-hdf-smoke.json` record three complete-file restorations with matching
SHA-256. Restored outputs used fresh destinations; no server original was replaced.

These checks concern archival integrity and deletion scope, not solver correctness
or GPU performance.
