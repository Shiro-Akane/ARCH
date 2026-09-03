# H100 SM90 GPU-AMR qualification evidence

This directory records the 2026-09-03 qualification run. JSON files are the
machine-readable authority; Markdown summarizes them without replacing them.

## Environment

| Item | Value |
| --- | --- |
| Host | remote Linux GPU server |
| GPU | NVIDIA H100-20C vGPU, SM90, 20,480 MiB |
| Driver | 570.133.20 |
| CUDA toolkit | 12.8.93 |
| Host compiler | GCC 11.4.0 |
| CMake / Ninja | 4.4.0 / 1.13.2 |
| Build | Debug, CUDA on, KLU off, SM90, serial |
| Build memory boundary | `MemoryMax=16G`, `MemorySwapMax=0` |

The requested GCC 12 host compiler was not installed on the server, so the run
used GCC 11.4.0 and records that deviation explicitly.

## Build result

| Target | Result | Elapsed | Maximum RSS | Swap |
| --- | --- | ---: | ---: | ---: |
| clean `arch_cuda_backend` | pass | `1:11:37` | `4,466,132 KiB` | `0` |
| complete `ARCH` link | pass | `1:54.77` | `1,206,568 KiB` | `0` |
| all CUDA test targets | pass | `20:31.74` | `4,807,348 KiB` | `0` |

Artifacts:

- `libarch_cuda_backend.a`: 174,611,782 bytes, SHA-256
  `ea789c913caa1091433fd0c82ad340ee0679dc32ee1e5eb68d390ef00a6eedf8`;
- `ARCH`: 202,176,184 bytes, SHA-256
  `d42711fa4a19357713be49df77cd3ab714faf4ccfe89c5d9bb16efa09231490b`.

The three `*-time.txt` files are the raw `/usr/bin/time -v` records.
`tu-memory-samples.csv` contains one-second cgroup samples attributed to the
active CUDA source. These values include build-scope overhead; they are useful
for locating heavy translation units but are not isolated process RSS.

## Production AMR result

`backend-validation-evidence.json` contains ten passed cases and 27 passed
CPU/CUDA checkpoint comparisons. The maximum field-normalized difference is
`9.99201e-16`.

The matrix includes Euler, RK2, RK3, a dynamic topology cycle, RKL1, RKL2 with
2/3/5 stages and negative `gamma`, plus 2D and 3D cases. Focused CUDA tests
separately cover X/Y/Z, both coarse/fine orientations, and all three field
slots.

## Restart result

Both restart evidence files contain five passed routes:

- uninterrupted CPU versus CUDA;
- CPU to CPU;
- CUDA to CUDA;
- CPU to CUDA;
- CUDA to CPU.

`restart-smooth-evidence.json` is exactly equal at all reported comparison
points. `restart-enuc-evidence.json` passes the declared scale-aware tolerance;
its maximum normalized field and ENUC difference is `6.71411e-4`.

## CUDA test and safety status

The first full CTest run passed 52 of 65 tests. All 13 failures were allocation
failures in NSE or aprox19/aprox21 burn tests while two unrelated Python jobs
occupied about 9.4 GiB of the 20 GiB vGPU. `ctest-initial.log` is retained as
the unmodified record. A resource-isolated rerun must replace this paragraph
only after it actually completes.

Five AMR lifetime/exchange/hydro/diffusion tests pass with
`CUDA_LAUNCH_BLOCKING=1`; see `cuda-launch-blocking.log`.

The four `arch-gpu-amr-final-sanitizer-*.log` files record attempted memcheck
and racecheck runs. NVIDIA reports `GPU debugging features are disabled` for
this vGPU profile, so no sanitizer pass is claimed. Qualification requires a
bare-metal GPU or a vGPU profile with CUDA debugging enabled.

## Evidence inventory

- `backend-validation-evidence.json`: production CPU/CUDA AMR matrix;
- `restart-smooth-evidence.json`: SmoothAdvection restart routes;
- `restart-enuc-evidence.json`: ENUC-driven restart and regrid routes;
- `ctest-initial.log`: full-suite run under external GPU contention;
- `cuda-launch-blocking.log`: synchronous-launch focused regression;
- `arch-gpu-amr-final-*-time.txt`: raw build resource records;
- `tu-memory-samples.csv`: per-source sampled build-scope memory peaks;
- `arch-gpu-amr-final-sanitizer-*.log`: unsupported sanitizer attempts.
