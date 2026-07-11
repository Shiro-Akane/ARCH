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
