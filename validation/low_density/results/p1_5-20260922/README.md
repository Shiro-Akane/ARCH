# P1.5 acceptance evidence — 2026-09-22

This package accompanies the source commit containing this directory on
`physics/selfgravity`, based on `99793b49887418d3fa4f151f9ad6b571b528f708`.
The untracked Studio workspace is excluded. P2 and production self gravity are
not implemented by this package.

## Scope and environment

- CPU: Debug build, OpenMP with four threads; full CTest and 72 physical runs.
- CUDA: Release build, CUDA 12.3, GCC 12, native `sm_86`; RTX 3060 Ti 8 GiB,
  driver 610.47. KLU and cuDSS are enabled. The same Release executable also
  executes all 72 cases with its CPU backend.
- Host compilation disables fast math and FMA contraction. CUDA uses
  `--fmad=false --ftz=false --prec-div=true --prec-sqrt=true`.
- Python validation uses NumPy, h5py and mpmath. Executables, EOS data, generated
  HDF5 and scratch build directories are not redistributed in this record.
- `cpu/identity.json` and `gpu/identity.json` bind the physical matrices to actual
  executable hashes, build settings and a representative compilation command.
  `gpu/sparse-libraries.json` additionally binds linked sparse-provider archives
  and configured lazy cuDSS/cuBLAS dependencies.

## Acceptance

CPU CTest: **55/55**. CUDA-build CTest: **129/129** (113 frozen targets plus
16 final burn-policy cases, disjoint and complete). Tooling: **357/357**.
Physical matrices: **72 CPU Debug + 72 CPU Release + 72 CUDA Release**.
Sanitizers: **16/16**, memcheck and racecheck on eight current executables.
No failed or skipped case is counted as passing. `summary.json` lists the full
CUDA test inventory. Hosted CI results are separate from this local acceptance.

## Evidence map

| Evidence | Content |
|---|---|
| `cpu/ctest.txt` | Full CPU CTest run |
| `cpu/presentation-ctest.txt`, `cpu/metadata-test.txt` | Final metadata-only follow-up |
| `cpu/physical-runs.json` | 72 CPU Debug runs with effective parameters and metrics |
| `cpu/release-physical-runs.json` | 72 runs of the final CUDA-enabled binary using CPU |
| `gpu/ctest-core.txt`, `gpu/ctest-burn.txt` | Complete CUDA-build CTest union |
| `gpu/physical-runs.json` | The corresponding 72 GPU runs |
| `gpu/cpu-cuda-comparison.json` | Same end times/topology, scaled state/species/ledger errors |
| `gpu/api-sparse-focused.txt`, `gpu/api-memory.json` | CPU API budget and real sparse solve after lazy loading |
| `checks/tooling.txt` | Python tooling regression |
| `checks/independent-flux.json` | Independent 70/90-digit Euler flux oracle |
| `checks/architecture.txt` | Shared-implementation architecture audit; empty means exit 0 |
| `sanitizers/` | Actual compute-sanitizer reports and executable identities |

The manifest fixes acceptance budgets. Physical cases include independent
entropy-wave convergence, rarefaction and acceleration references; CPU/GPU
agreement alone is not an independent physical oracle. The comparison finds
identical fluid fields and repair ledgers, with maximum species difference
`1.5543122344752192e-15`. Only the first repaired-cell index is omitted from the
cross-device comparison because concurrent repairs can select different valid
witness cells; event counts and physical ledger fields remain checked.

## Limits and cost

Full evolution is qualified for the stated positive-density IdealGas cases down
to `rho=1e-30`. Mathematical leaves extend through `rho=1e-100`. This does not
certify exact vacuum, kinetic-energy-dominated unresolved thermal residuals,
or extrapolation beyond an EOS source domain. Invalid states fail explicitly.

Repair accounting adds `(10 + 2*N_species)*8` bytes per block per Hydro stage at
an existing completion point. Reflux adds one status integer at its existing
synchronization. The multiblock tests retain the one-sync-per-batch requirement.
No cell state is downloaded for a host repair or sparse solve.

The CFL policy now includes both faces, so a given configured CFL can require
more steps than the retired policy. Output thermodynamic checks add EOS queries
at the existing output materialization point. These are explicit behavior/cost
changes. Per-case times include startup and I/O and were collected while CUDA
test compilation was running; these small cases are **not throughput evidence**
and do not establish a GPU speedup or a before/after performance guarantee.

On Linux/WSL, configured sparse libraries are mapped only on the first sparse solver request.
At the same pre-limit inspection boundary, virtual address space falls from
1,124,072 KiB to 300,780 KiB; the 1 GiB API cap is unchanged. Library paths are
build-owned; moving them requires reconfiguration.

## Reproduction

Use `validation/low_density/README.md` for the two matrix commands. Use the normal
CTest entry points for the CPU and CUDA build directories, and run memcheck and
racecheck against the executables named by the sanitizer evidence. Each physical
matrix must use a fresh output directory. The temporary absolute paths in the
records identify this execution, not required paths on another user's machine.

Native Windows retains direct import-library linking; this package qualifies Linux/WSL execution.
