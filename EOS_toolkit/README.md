# Runtime EOS tables

Chinese translation: [README.zh-CN.md](README.zh-CN.md). The English file is
the authoritative source text.

An equation-of-state (EOS) table supplies thermodynamic data used during a
simulation, such as the relationship between density, temperature and energy.
Choose a table that matches the EOS selected in the parameter file; the
[table interface](../src/physics/eos/TabularEOS.md) describes normalized 3D/4D
HDF5, EOSDriver total-EOS HDF5 and the original positive-temperature baryon
ASCII main tables used by Shen EOS2/EOS4.

This directory contains only versioned runtime EOS data. Generators, exploratory
tables, and upstream source snapshots stay out of the runtime data tree. Tables
are grouped by model or dataset:

```text
EOS_toolkit/
├── README.md
├── README.zh-CN.md
└── tables/
    ├── helmholtz/
    │   └── helm_table.dat
    └── baryon/
        ├── eos2.tab
        └── eos4.tab
```

Add future production tables under `tables/<model-or-dataset>/`; do not place
data files at the `EOS_toolkit/` root. Each new table must document its source,
layout, checksum, redistribution terms, and at least one validation record.
Parameter files use a repository-root-relative or absolute path:

```ini
eos_type = helmholtz
eos_table_path = EOS_toolkit/tables/helmholtz/helm_table.dat
```

Source tables may remain outside the repository. Point the existing tabular
policy directly to a supported source file; copying it into this directory or
converting it into a second table is unnecessary:

```ini
eos_type = tabular
eos_table_path = /absolute/path/to/eos2.tab
use_burn = false
# Optional: defaults to the existing table below, used only for missing electrons.
eos_helm_table_path = EOS_toolkit/tables/helmholtz/helm_table.dat
```

The loader reads the source's declared components, or the documented components
of a recognized native format. For a partial free-energy table, it adds only
missing electron/positron and photon contributions at loading, keeping the
source baryons intact. Complete tables and photon-only completion do not need
the electron table. Normalized HDF5 producers declare `eos_components`; optional
`baryon_mass_g` records a fixed source mass convention. There is no numerical
guess of a table's components and no per-EOS physical tuning file.

The [interface guide](../src/physics/eos/TabularEOS.md) specifies the fixed energy
references, F/P/S constraints, excluded source/component stencils, strict
temperature inversion and equilibrium/burn restrictions. A source that loads
is not thereby qualified throughout its domain. Arbitrary CompOSE layouts and
the zero-temperature/zero-charge auxiliary ASCII tables are not supported by
the positive-temperature main-table reader.

## Original Shen data

The authors' [EOS2/EOS4 archive](https://zenodo.org/records/3612487) permits
redistribution of its identified original files under CC BY 4.0. ARCH includes
the two unmodified main-table members through Git LFS at:

| Runtime asset | Bytes | SHA-256 of the unpacked original member |
| --- | ---: | --- |
| `tables/baryon/eos2.tab` | 143,166,842 | `d52d37d30fec10ffb5279689a172e61a7ebb3538a4acaf2270dc32469c0d3c58` |
| `tables/baryon/eos4.tab` | 143,167,115 | `5ee37819f873387af9c38207bcada72a48abe695b9491df9ad5c6a5e69487c34` |

The two unpacked tables total 286,333,957 bytes. Download their LFS contents
before use; a pointer-only checkout does not contain usable EOS data. An
external path to the identical member is also valid. Unpacking changes neither
their samples nor their license. Attribution is to H. Shen, F. Ji, J. N. Hu
and K. Sumiyoshi, *Equation of state for simulations
of core-collapse supernovae and neutron-star mergers* (2020),
[DOI: 10.5281/zenodo.3612487](https://doi.org/10.5281/zenodo.3612487).
Preserve the [license and provenance notice](../THIRD_PARTY_NOTICES.md#external-shen-eos-tables-and-eosdriver-compatible-formats).
The six-file upstream deposit also contains `.t00` and `.yp0` products; their
presence in the archive does not imply support by this reader.

Processed HShen HDF5 data are not bundled. Their EOSDriver format has been
investigated for compatibility, but their distributor's separate terms must
be checked; the original archive's CC BY license does not relicense them.

The file `helm_table.dat` is the specific table member extracted from the project's downloaded Timmes `helmholtz.tar.xz` archive. It is strictly tracked through Git LFS, meaning the large table data is stored entirely separately from the small pointer found in an ordinary Git checkout. You must explicitly download the LFS data before attempting to run any case that relies on the Helmholtz EOS. Essential provenance, redistribution scope, required size constraints, and checksums are rigorously recorded in [`THIRD_PARTY_NOTICES.md`](../THIRD_PARTY_NOTICES.md) and [`validation/burn/README.md`](../validation/burn/README.md).
