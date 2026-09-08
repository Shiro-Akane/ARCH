# Source-delivery asset observations

This is the companion to [delivery-assets-audit.json](delivery-assets-audit.json),
collected on 2026-09-07 from 18:49:05 to 18:55:01 UTC. It records the current
worktree and available dependencies without declaring a manual delivery gate
passed, rerunning validation, or granting redistribution permission.

The shared provenance helper observed source scope
`73a9cf50bbd4405972160ecb1742da33f52666466b33d1fa8d5d1949cffed171`
on both reads during the inventory. File identities use the same maintained
`validation_provenance` helpers as the validation evidence.

## Maintained inputs

The code/input scope contains 316 tracked paths and 139 untracked paths.
452 files are present; the only three absent tracked inputs are the retired
CUDA headers listed below. All 139 untracked inputs are present and none is
ignored by Git. Their complete paths, byte counts and SHA-256 values are in
the JSON inventory.

| Untracked source area | Files |
|---|---:|
| `cmake` | 8 |
| `src` | 45 |
| `simulation` | 2 |
| `tests` | 57 |
| `tools` | 10 |
| `validation` inputs and runners | 17 |

These files are part of the frozen worktree, but are not yet in a commit.
A source delivery must include them; a HEAD-only archive would omit them.
No staging, commit, push or packaging was performed for this audit.

The retired headers are `DiffusionConfigViewAdapter.h`,
`CudaBackendBurnAllNetworksImpl.cuh`, and `CudaBurnNetworkTypes.h`. Searching
the maintained CMake, implementation, simulation, test, tool and validation
inputs found no active include or build consumer. The two remaining name
mentions are deliberately constructed negative-test fixtures for the retired
diffusion adapter, not dependencies on the deleted file. The JSON preserves
the search and each removed header's HEAD blob identity. The removed
`validation/network/metrics.csv` is outside the code/input scope; current
structured evidence remains separate from that old summary.

## EOS and generated-network assets

The actual Helmholtz table is present at
`EOS_toolkit/tables/helmholtz/helm_table.dat`. Its 60,242,514 bytes and SHA-256
`c9a57c26c6fd2b2b378b9d5295ca1214022f6fec6289d038b47bf8c8938881a1`
match the Git LFS pointer in both the index and HEAD. A fresh checkout needs
the LFS object, not only its pointer text.

The configured `audit31` and `weak_urca` packages contain exactly 25 files
each. All 50 installed files match their current recovery-record identities
and are byte-identical to the corresponding durable recovery copies. The
manifests also match the combined current generator identity. The historical
audit31 manifest's generator hash was intentionally updated during recovery;
this audit does not claim that every current manifest is identical to every
earlier record.

Installed packages currently live in an external `/tmp` root; their recovery
copies live under ignored `build/`. They are not automatically included in a
Git source archive. The maintained generator modules and the `audit31.py` and
`weak_urca.py` recipes are present for local regeneration. The default custom
package root contains its documentation and an empty `cmake_probe` directory,
not an installed generated network. An exact generated-package distribution
would need all package files, applicable upstream notices and rate-data
provenance in addition to ARCH's source.

## Build and setup references

The reviewed English/Chinese setup documents name available generator modules,
recipes, CMake CUDA route modules and templates, the cuDSS discovery module,
and the memory guard. All five generated-network validation targets named in
the setup guide appear in the current Ninja manifest. The configured cuDSS
0.8 header, library and complete package notice are present. These are
file-level availability observations, not a new configuration or provider run.

The local build targets CUDA architecture 86, and its compile inventory
contains native CPU optimization. It is not a portable binary distribution:
recipients rebuild for their CPU, select compatible CUDA code images and
install suitable runtime/provider libraries. cuDSS remains separately
installed; this source audit does not bundle its binaries.

Build outputs, results, generated checkpoints/plots, Python caches, external
network packages and third-party provider binaries are recorded or excluded
according to their roles, rather than counted as maintained ARCH code.
No clean-clone build, fresh generation, GPU test or full acceptance-index run
was performed here.

## Attribution and contact

The current third-party notices attribute the Timmes-derived networks, NSE,
Helmholtz code and table and state that the author has not yet been contacted.
Contact and redistribution confirmation remain pending. Attribution, correct
LFS materialization and successful technical tests do not establish permission.
This administrative item and final release qualification remain separate from
the asset observations above.
