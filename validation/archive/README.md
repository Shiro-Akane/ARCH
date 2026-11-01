# Validation data availability

[中文](README.zh-CN.md) · [Validation index](../README.md)

The repository stores compact measurements, parameter files, independent
reference samples and reproduction scripts with each module. Complete
application transcripts and per-step traces are indexed separately, so a source
checkout contains the data needed to understand each numerical result without
all generated working output.

[raw-records-20261006.json](raw-records-20261006.json) identifies 9,549 archived
files, totaling 147,038,657 bytes, by their module, original filename and source
commit. These files were ordinary Git objects and can be recovered from that
commit:

```sh
git show <source_commit>:<repository_relative_path> > /tmp/recovered-record
```

The producing machine also retains the payloads under
`studio/.local/validation-archive/20261006/`, followed by the original
repository-relative path. Its local `manifest.json` records paths, sizes, Git
blob identities and preservation checks. This local payload is separate from
a fresh clone.

Paths in result JSON identify the files used by that execution. A path may
therefore describe a local file rather than a distributed fixture. Consult the
record's result status as well as its metrics: for example, the retained
audit31 memcheck timeout is an incomplete execution, not a successful check.

Runtime EOS tables remain in `EOS_toolkit/`; regression inputs remain in
`tests/fixtures` and module `inputs/` directories. Tests generate their own state
outputs. Large checkpoints, plotfiles and complete traces from new measurements
should be indexed with their source and input identities; publish a separate
data archive when a study requires those outputs for independent analysis.
