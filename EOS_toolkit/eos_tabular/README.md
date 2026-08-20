# Tabular EOS assets

Chinese translation: [README.zh-CN.md](README.zh-CN.md). The English file is
the authoritative source text.

Runtime EOS tables are grouped by model under this directory:

```text
eos_tabular/
└── helmholtz/
    └── helm_table.dat
```

Add future tables under a descriptive model or dataset subdirectory rather
than at the `EOS_toolkit/` root. Parameter files must use the repository-root
relative path or an absolute path, for example:

```ini
eos_type = helmholtz
eos_table_path = EOS_toolkit/eos_tabular/helmholtz/helm_table.dat
```

`helm_table.dat` is the table member from the project's downloaded Timmes
`helmholtz.tar.xz` archive. It is tracked through Git LFS and is distinct from
the original Fortran source retained in `EOS_toolkit/helmholtz/`. Provenance,
redistribution scope, required size, and checksum are recorded in
[`THIRD_PARTY_NOTICES.md`](../../THIRD_PARTY_NOTICES.md) and
[`validation/burn/README.md`](../../validation/burn/README.md).
