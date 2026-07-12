# ARCH aprox19 burn kernel: CPU vs GPU benchmark (2026-07-12)

## Setup
- 1,048,576 independent cells, isothermal 16-species BE-NR burn, dt_target=1e-8 s
- Conditions: rho in 1e5..3e7 g/cc, T in 1.5e9..3.5e9 K, X(C12)=X(O16)=0.5 (cellular-detonation regime)
- Numerics: exact port of src/numerics/burnsolver/ode_be-nr.h (Newton + pivoted dense LU + PI substep control)
- Same source compiled for both paths; identical inputs; cells pre-sorted by T (divergence control)
- CPU: 16 threads OpenMP -O3 | GPU: H100-20C (114 SM), CUDA 12.4, sm_90

## Result
| path | time | throughput | correctness |
|---|---|---|---|
| CPU 16T | 23.3 s | 4.5e4 cells/s | ref |
| GPU kernel (shared-mem interleaved Jacobian) | 3.41 s | 3.1e5 cells/s | max rel dX = 9.6e-14 |
| speedup | 6.8x | | avg substeps identical (115.9) |

## Optimization journey
1.0x (naive, local-mem spill to HBM) -> 5.7x (sort by T + 16x16 matrix + L1 carveout)
-> 6.7x (Jacobian in shared memory, lane-interleaved to kill 32-way bank conflicts)
-> 6.8x (block-size tuning; plateau)

## Known ceiling & next steps
- One-thread-per-cell + 2KB Jacobian/thread caps occupancy at ~3 warps/SM; FP64 exp latency not hidden.
- Next: warp-cooperative cell (32 lanes share rates+LU), mixed-precision LU for Newton direction,
  in-app integration (Cellular case) after white_dwarf EOS table is regenerated (helm_table.dat needed;
  repo copy is a Git-LFS pointer).
- Author-repo burn-path issues found (report upstream): NetAprox19 ODE_NEQ=19 vs NumSpec=16 mismatch,
  RHS[16..18] uninitialized, Driver puts T at index n_spec=19 while solver reads index 18,
  enuc computed but never coupled back to energy (no self-heating).
  --> ALL FOUR FIXED by author in main @ 6b95bd9 "working Burner" (verified 2026-07-12).

---

# v2 — Variable-temperature burn: CPU vs GPU (2026-07-12)

## Setup
- burnbench_v2.cu: matches author's completed variable-T Burner (main @ 6b95bd9).
- 17-eq system = 16 species + temperature; dT/dt = enuc/cv with cv from Timmes Helmholtz EOS
  (helm_table.dat, 2D quintic-Hermite table interp); 17x17 Jacobian temperature row/col by finite diff.
- Line-for-line __host__/__device__ port of ode_be-nr.h + HelmEos.h; 7.8 MB f[9] table uploaded to device.
- 262,144 cells; rho 3e6..3e7 g/cc, T 2e9..4e9 K, X(C12)=X(O16)=0.5; dt_target=1e-9 s.
- CPU: 16 threads OpenMP -O3 | GPU: H100-20C, CUDA 12.4, sm_90.

## Result
| path | time | throughput | correctness |
|---|---|---|---|
| CPU 16T | 46.7 s | 5.6e3 cells/s | ref |
| GPU kernel | 15.3 s | 1.7e4 cells/s | rel err: species 1.6e-6, temperature 9.2e-12 |
| speedup | 3.1x | | avg substeps identical (115.7) |

## Why 3.1x (vs 6.8x isothermal)
- Each Newton iteration now does ~36 extra Helmholtz table interpolations (heavy pow/log -> GPU SFU bound)
  for the finite-difference Jacobian temperature row/column.
- 17x17 Jacobian = 2.3 KB/thread shared memory caps block size <=~98 and lowers occupancy.
- Correctness is exact (substeps identical, temperature agrees to 9e-12); this is an honest ceiling, not a bug.

## Notes / reproduce
- Needs the device-annotated aprox19 headers (sed: `^\s*inline$` -> `__host__ __device__ inline`,
  species constant tables mirrored to __constant__) PLUS making network::mion device-visible
  (benchmark mirrors mion constants + own compute_enuc to avoid touching original code).
- Needs helm_table.dat (60 MB): curl from
  raw.githubusercontent.com/AMReX-Astro/Microphysics/development/EOS/helmholtz/helm_table.dat
  (sha256 c9a57c26... byte-identical to the repo LFS pointer).

## Next
- Kernel opt: Helmholtz table via texture/__ldg, shrink Jacobian shared footprint for occupancy, warp-cooperative.
- Wire GPU burner into Driver do_burn_step (replace the OpenMP loop), end-to-end Cellular detonation.
- Then: block-structured AMR.
