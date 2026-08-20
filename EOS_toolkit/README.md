# Runtime EOS tables

Chinese translation: [README.zh-CN.md](README.zh-CN.md). The English file is
the authoritative source text.

This directory contains only versioned runtime EOS data. Generators, exploratory
tables, and upstream source snapshots stay out of the runtime data tree. Tables
are grouped by model or dataset:

```text
EOS_toolkit/
├── README.md
├── README.zh-CN.md
└── tables/
    └── helmholtz/
        └── helm_table.dat
```

Add future production tables under `tables/<model-or-dataset>/`; do not place
data files at the `EOS_toolkit/` root. Each new table must document its source,
schema, checksum, redistribution terms, and at least one validation record.
Parameter files use a repository-root-relative or absolute path:

```ini
eos_type = helmholtz
eos_table_path = EOS_toolkit/tables/helmholtz/helm_table.dat
```

`helm_table.dat` is the table member from the project's downloaded Timmes
`helmholtz.tar.xz` archive. It is tracked through Git LFS. Provenance,
redistribution scope, required size, and checksum are recorded in
[`THIRD_PARTY_NOTICES.md`](../THIRD_PARTY_NOTICES.md) and
[`validation/burn/README.md`](../validation/burn/README.md).
