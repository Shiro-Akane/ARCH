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

The Timmes download pages request citation of the relevant publications and
contact with the author when these codes, pieces, or modified versions are
used. No standard SPDX software license is stated on those pages. The project
therefore does not assert that the Timmes-derived material is relicensed under
ARCH's MIT license. Maintainers should preserve source attribution and confirm
applicable redistribution terms before a public release.

Implementation and validation details are in
[`docs/physics/TimmesNetworks.md`](docs/physics/TimmesNetworks.md).

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

## ARCH support code inside attributed directories

Directory placement alone does not imply third-party authorship. For example,
`timmes_common/Dual.h`, `RatePair.h`, and `TimmesNetworkSupport.h` are
ARCH-authored support layers around Timmes-derived equations. Their file
headers state that boundary explicitly. Other ARCH modules have no external
provenance claim unless a file header or this notice says otherwise.
