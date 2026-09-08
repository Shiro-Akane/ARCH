# Third-Party Provenance and Notices

Chinese translation: [THIRD_PARTY_NOTICES.zh-CN.md](THIRD_PARTY_NOTICES.zh-CN.md).
The English file is the authoritative source text.

The MIT license in [`LICENSE`](LICENSE) applies to ARCH-authored material.
Files adapted from third-party scientific software retain their upstream
provenance and terms. This notice does not replace an upstream license or grant
additional rights.

## Frank Timmes scientific software

ARCH contains C++ adaptations of software distributed by Frank Timmes:

| ARCH area | Immediate upstream source | ARCH contribution |
| --- | --- | --- |
| `src/physics/network/{iso7,aprox13,aprox19,aprox21}/` | `public_iso7.f90`, `public_aprox13.f90`, `public_aprox19.f90`, and `public_aprox21.f90` from the [Timmes reaction-network page](https://cococubed.com/code_pages/burn.shtml) | C++ policy interfaces, generic solver coupling, generated derivatives, and CPU/GPU-portable organization |
| `src/physics/nse/nse_solver.h` | `public_nse` from the [Timmes NSE page](https://cococubed.com/code_pages/nse.shtml) | compile-time network coupling, safeguards, compact-network handling, and energy closure |
| `src/physics/eos/HelmEos.h` | Helmholtz EOS package from the [Timmes EOS page](https://cococubed.com/code_pages/eos.shtml) | C++ EOS policy, strict table loading, state coupling, and diagnostics |
| `EOS_toolkit/tables/helmholtz/helm_table.dat` | `helm_table.dat` from the project's downloaded `helmholtz.tar.xz` archive | Git LFS packaging only; the table data are not an ARCH-authored work |

The Timmes download pages request citation of the relevant publications when
these codes, pieces, or modified versions are used. The reaction-network page
also invites contact about integration into other software. No standard SPDX
software license is stated on those pages. The project
therefore does not assert that the Timmes-derived material is relicensed under
ARCH's MIT license. Maintainers should preserve source attribution and confirm
applicable redistribution terms before a public release.

The maintainer is handling the request to clarify redistribution of the Timmes
material and will retain the correspondence and applicable terms. Source
attribution and upstream notices remain in place while this request is handled.
This administrative release item is separate from technical validation; an
in-progress request, attribution or successful tests does not establish written
redistribution permission.

Implementation and validation details are in
[`docs/physics/TimmesNetworks.md`](docs/physics/TimmesNetworks.md).

## External Shen EOS tables and EOSdriver-compatible formats

ARCH's native nuclear-table reader is an ARCH-authored implementation of data
formats. It does not copy or adapt EOSmaker/EOSdriver routines. Reader code and
table data have separate provenance: support for a table format does not
transfer ownership or licensing of the table data to ARCH.

The authors' [Shen EOS archive on Zenodo, record 3612487](https://zenodo.org/records/3612487)
declares `cc-by-4.0` in its [record metadata](https://zenodo.org/api/records/3612487).
That deposit contains six ZIP archives for the EOS2 and EOS4 `.tab`, `.t00`
and `.yp0` tables. Those specifically archived files may be redistributed and
adapted under [Creative Commons Attribution 4.0 International](https://creativecommons.org/licenses/by/4.0/),
including for commercial purposes. Preserve the supplied creator attribution,
source/DOI, copyright, license and disclaimer notices; provide the license link
and identify modifications, including prior modification notices. Do not imply
author endorsement or impose restrictions that contradict the license. The
table data retain their own license when packaged alongside ARCH's MIT code.

ARCH includes the unmodified `eos2.tab` and `eos4.tab` main-table members from
that deposit at `EOS_toolkit/tables/baryon/`, stored through Git LFS. Attribution:
H. Shen, F. Ji, J. N. Hu and K. Sumiyoshi, *Equation of state for simulations of
core-collapse supernovae and neutron-star mergers* (2020),
[DOI: 10.5281/zenodo.3612487](https://doi.org/10.5281/zenodo.3612487).
Only extraction and LFS packaging were performed; the data bytes were not
modified. Exact sizes and SHA-256 checksums are recorded in the
[runtime-table guide](EOS_toolkit/README.md#original-shen-data).
ARCH's in-memory component completion and fixed energy reference do not alter
these distributed source files. The zero-temperature `.t00` and zero-charge
`.yp0` auxiliary products are not bundled.

This recorded license applies to the identified deposit, not automatically to
every Shen release or a third party's processed table. The
[StellarCollapse EOS page](https://stellarcollapse.org/equationofstate.html)
states attribution, noncommercial and share-alike conditions for its code and
tables. Redistributing an EOSdriver-compatible HShen HDF5 table therefore
requires checking the terms and notices for that particular processed file and
version, including contributions added during its preparation. The original
Zenodo archive's CC BY 4.0 license does not automatically replace those separate
terms. Processed HShen HDF5 tables have been investigated for format
compatibility only and are not bundled with ARCH.

## AMReX-Astro Microphysics

`src/physics/diffusionCoe/diffusion_math.hpp` is a framework-independent C++
adaptation of
[`conductivity/stellar/actual_conductivity.H`](https://github.com/AMReX-Astro/Microphysics/blob/6fb41b5f7475b42a06eb5b09ff0520c9f08aa7f0/conductivity/stellar/actual_conductivity.H)
from AMReX-Astro Microphysics, audited at commit
`6fb41b5f7475b42a06eb5b09ff0520c9f08aa7f0`. ARCH supplies the callable
interface and EOS/species integration; the conductivity formulas and fitted
constants follow the upstream implementation.

The retained upstream license is in
[`LICENSES/AMReX-Astro-Microphysics.txt`](LICENSES/AMReX-Astro-Microphysics.txt).
Its copyright notice, conditions, and disclaimer must remain with source
redistributions; binary redistributions must reproduce them in accompanying
documentation or other materials.

## SuiteSparse KLU sparse solver

ARCH can fetch pinned SuiteSparse v7.13.0 and statically link KLU with its minimal BTF, AMD, COLAMD, and SuiteSparse_config dependencies. KLU and BTF are LGPL-2.1-or-later; AMD, COLAMD, and SuiteSparse_config are BSD-3-Clause. The retained component notices are in [`LICENSES/SuiteSparse-KLU.txt`](LICENSES/SuiteSparse-KLU.txt), and the complete LGPL-2.1 text is in [`LICENSES/LGPL-2.1.txt`](LICENSES/LGPL-2.1.txt). Source and binary redistribution must satisfy the applicable upstream terms; ARCH MIT terms do not relicense these components.

## pynucastro-generated networks and nuclear data

ARCH supplies generation recipes and portable adapters in [`tools/network/`](tools/network/).
The maintained [`audit31` and `weak_urca` workloads](validation/network/README.md)
use pynucastro 2.12.0. Users generate their network packages locally; the ARCH
adapters do not make the emitted templates or nuclear data ARCH-authored material.

pynucastro 2.12.0 is distributed under its
[BSD-3-Clause license](https://raw.githubusercontent.com/pynucastro/pynucastro/2.12.0/LICENSE).
SimpleCxx output incorporates upstream templates, including an `amrex_bridge.H`
whose source comments identify adaptations from AMReX and Microphysics. Preserve
those attributions and applicable terms when redistributing generated packages,
including the pynucastro license and the relevant
[AMReX](https://raw.githubusercontent.com/AMReX-Codes/amrex/development/LICENSE)
and Microphysics notices. The existing Microphysics notice above concerns ARCH's
conductivity adaptation; it is not a blanket license for every generated file.
Source distributions must retain the applicable copyright notices, conditions,
and disclaimers; binary distributions must reproduce them in accompanying
materials. pynucastro's [citation guide](https://pynucastro.github.io/pynucastro/citing.html)
requests its 2.0 paper and Zenodo software citation.

The rate data have their own scientific provenance. `audit31` uses the
[JINA ReacLib database](https://reaclib.jinaweb.org/index.php), whose requested
database citation is Cyburt et al., *ApJS* 189, 240 (2010). `weak_urca` uses the
Na-23/Ne-23 electron-capture and beta-decay tables supplied with pynucastro from
[Suzuki, Toki, and Nomoto, *ApJ* 817, 163 (2016)](https://doi.org/10.3847/0004-637X/817/2/163).
pynucastro's [third-party data guide](https://pynucastro.github.io/pynucastro/sources.html)
links these sources, the author's Suzuki tables, and the nuclear-property and
partition-function references. Keep the selected rates, table provenance, and
relevant citations with a redistributed network. This notice does not assign
ARCH's MIT license or a blanket software license to those scientific data.

## NVIDIA cuDSS

cuDSS is an optional, separately installed NVIDIA sparse-solver provider; ARCH's
source distribution does not bundle its binaries. The reviewed integration uses
the 0.8 API and the `nvidia-cudss-cu12` 0.8.0.10 package. Installation instructions
are in NVIDIA's [cuDSS guide](https://docs.nvidia.com/cuda/cudss/getting_started.html).

cuDSS is governed by the
[NVIDIA Math Libraries SDK license agreement](https://docs.nvidia.com/cuda/cudss/license.html),
not ARCH's MIT license. Any redistribution of SDK components must follow the
agreement and complete notices supplied with that particular package. The
reviewed package's `LICENSE.txt` also contains AMD/COLAMD, METIS, fmt, and HSL
notices, which belong with the complete package notice rather than an excerpt
of the NVIDIA agreement alone.

## ARCH support code inside attributed directories

Note that directory placement alone does not inherently imply third-party authorship. For example, `timmes_common/Dual.h`, `RatePair.h`, and `TimmesNetworkSupport.h` are entirely ARCH-authored support layers built around Timmes-derived equations. Their respective file headers state this boundary explicitly. Similarly, all other ARCH modules have no external provenance claims whatsoever unless a specific file header or this notice explicitly says otherwise.
