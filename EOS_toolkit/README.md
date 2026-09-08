# Runtime EOS tables

Chinese translation: [README.zh-CN.md](README.zh-CN.md). The English file is
the authoritative source text.

An equation-of-state (EOS) table supplies thermodynamic data used during a
simulation, such as the relationship between density, temperature and energy.
Choose a table that matches the EOS selected in the parameter file; the
[table interface](../src/physics/eos/TabularEOS.md) describes the normalized HDF5
format for additional datasets.

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

The file `helm_table.dat` is the specific table member extracted from the project's downloaded Timmes `helmholtz.tar.xz` archive. It is strictly tracked through Git LFS, meaning the large table data is stored entirely separately from the small pointer found in an ordinary Git checkout. You must explicitly download the LFS data before attempting to run any case that relies on the Helmholtz EOS. Essential provenance, redistribution scope, required size constraints, and checksums are rigorously recorded in [`THIRD_PARTY_NOTICES.md`](../THIRD_PARTY_NOTICES.md) and [`validation/burn/README.md`](../validation/burn/README.md).
