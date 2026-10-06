# Raw validation record archive

[中文](README.zh-CN.md) · [Validation index](../README.md)

The repository retains reviewed measurements, immutable inputs, independent
reference samples and reproduction scripts. Complete application transcripts
and per-step execution traces have a local archive with their original source
identity. This keeps the scientific decisions readable without distributing
every working output in a source checkout.

[The 2026-10-06 index](raw-records-20261006.json) identifies 9,549 historical raw
files (147,038,657 bytes) and records the owning modules and exact filename
selectors. The files were unmodified tracked outputs before archival. Each was
moved atomically on the same filesystem, with device, inode, size and modification
time preserved and checked before and after the move. No scientific result,
failure status or Git history was rewritten.

The original machine retains the payloads under
`studio/.local/validation-archive/20261006/`, followed by each original
repository-relative path. Its local `manifest.json` contains the complete path,
size, original Git blob identity and preservation record; the compact index is
the published inventory. The local payloads and manifest are not part of a fresh
clone. Original regular-Git files remain recoverable from the source commit in
the index, for example:

```bash
git show <source_commit>:<original_repository_relative_path> > /tmp/recovered-record
```

Existing JSON artifact paths describe the original run and its measured
identities. An archived raw path is historical provenance, not a currently
distributed file. A missing local raw archive does not turn an old failed or
incomplete execution into a pass. The audit31 timeout in the
[preserved memcheck attempt](../backend/results/final-first-law-20260907/attempt-902/README.md)
remains an incomplete attempt; its application output is retained locally and
its sanitizer report remains with the published record.

Runtime EOS tables and licenses, `tests/fixtures`, parameter files, JSON/CSV
metrics, curated scientific sample TSV, scripts and sanitizer logs retain their
existing owners. Regression tests construct their own state outputs and use
maintained reference fixtures. Neither this raw archive nor FLASH is a build,
test or CI dependency. New complete traces and generated state remain local;
published verification records contain their compact metrics and input/source
identities.
