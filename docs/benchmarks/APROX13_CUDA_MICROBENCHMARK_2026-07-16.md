# Current aprox13 CPU/OpenMP–CUDA microbenchmark (2026-07-16)

This record measures only one batched translated-Timmes `aprox13` operator per
independent cell.  Each evaluation includes the RHS, the complete analytic
abundance Jacobian, the analytic temperature column and nuclear-energy row, and
formation of `I - dt J`.  It is **not** an ODE integration, Helmholtz EOS, NSE,
hydrodynamics, I/O, or end-to-end two-dimensional simulation benchmark.

The benchmark does not reference the legacy pynucastro CUDA mirror, the
standalone `archgpu` program, or any high-temperature freeze/bypass path.

## Reproducibility

- Repository base commit: `047775d08a0b0e450f9f131d6b43b793a24a3fdd`
- Branch: `codex/runtime-cuda-backend` (dirty integration worktree)
- GPU: NVIDIA H100-20C, compute capability 9.0, 20,480 MiB
- GPU UUID: `GPU-eac47d30-7ff6-11f1-a6ee-b04d62a4c76e`
- Driver: 570.133.20
- CUDA compiler/runtime: 12.4.131
- Host compiler: GCC 11.4.0
- CPU allocation: 16 virtual Intel Xeon Gold 6338 cores, one hardware thread
  per visible core
- Build: CMake Release (`-O3 -DNDEBUG`), CUDA architecture 90, normal
  production FMA, OpenMP enabled
- Affinity: `OMP_PROC_BIND=close`, `OMP_PLACES=cores`
- State for every cell: `rho=1e8 g cm^-3`, `T=2e9 K`, `cv=1.3e8`,
  `dt=1e-12 s`; the same normalized positive 13-species composition is copied
  to every cell
- Timing: one untimed CPU full-batch warmup; 10 untimed CUDA launches; 5 timed
  repeats; reported value is the median
- GPU kernel-only timing: CUDA events
- GPU end-to-end timing: synchronous pageable-host H2D input copy, kernel, and
  D2H checksum copy measured with `steady_clock`
- CUDA launch: 128 threads/block; compiler reports 254 registers/thread and
  6,544 bytes local memory/thread
- Benchmark source SHA-256:
  `a60dcda13cb0885faf321c9451c3044776e1edba4a4ce18a27a3a54a991a2e62`
- Executable SHA-256:
  `1e93694f3d34e8f3604cefe861deb25e19b04dc0e1c80e0b38d0e733283e26f9`

The formal run was started only after `nvidia-smi` reported no compute process
and the host process list showed no competing compiler or simulation job.

Configure and build:

```bash
cmake -S . -B build-decoupled-cuda \
  -DARCH_ENABLE_CUDA=ON -DARCH_ENABLE_OPENMP=ON \
  -DCMAKE_CUDA_COMPILER=/home/ubuntu/miniconda3/envs/archcuda/bin/nvcc \
  -DARCH_CUDA_ARCHITECTURES=90 -DCMAKE_BUILD_TYPE=Release
cmake --build build-decoupled-cuda \
  --target ARCH_cuda_aprox13_benchmark -j 1
```

Run:

```bash
OMP_PROC_BIND=close OMP_PLACES=cores \
./build-decoupled-cuda/bin/ARCH_cuda_aprox13_benchmark
```

## Operator parity

The Release/performance binary retained the required `1e-12` CPU–CUDA gate:

| Quantity | Scaled maximum relative error |
| --- | ---: |
| RHS | 7.181116744557e-13 |
| Complete Jacobian | 2.169440017504e-13 |
| Implicit LHS | 2.169301334644e-13 |
| Temperature column | 1.642068726019e-13 |
| Temperature/energy row | 2.169440017504e-13 |

Status: **PASS**.  The CPU, 16-thread OpenMP, and CUDA batch checksums also
matched at every batch size.

## Measured throughput

| Cells | CPU 1 thread (cells/s) | CPU 16 threads (cells/s) | GPU kernel-only (cells/s) | GPU H2D+kernel+D2H (cells/s) | Kernel speedup vs CPU1 / CPU16 | End-to-end speedup vs CPU1 / CPU16 |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 1,024 | 3.095308e4 | 4.917183e5 | 2.186539e6 | 1.945621e6 | 70.640x / 4.447x | 62.857x / 3.957x |
| 16,384 | 4.955154e4 | 4.937408e5 | 2.164630e7 | 1.697483e7 | 436.844x / 43.841x | 342.569x / 34.380x |
| 65,536 | 4.956877e4 | 4.957624e5 | 2.766857e7 | 2.129688e7 | 558.186x / 55.810x | 429.643x / 42.958x |
| 262,144 | 4.961293e4 | 4.956210e5 | 3.319771e7 | 2.341266e7 | 669.134x / 66.982x | 471.906x / 47.239x |

The small 1K batch is launch/occupancy limited.  Larger batches amortize launch
and transfer costs.  These ratios compare the current CPU generic analytic-AD
Jacobian implementation with the current CUDA explicit analytic aprox13
Jacobian implementation, so they are implementation-level operator speedups,
not a claim about GPU hardware alone.

Do not extrapolate these numbers to Cellular or any other 2-D run.  A defensible
application speedup still requires the same Helmholtz EOS, source splitting,
ODE/Newton iterations and dense solve, NSE transition, hydro, boundaries,
species transport, timestep sequence, and output policy on both backends.
