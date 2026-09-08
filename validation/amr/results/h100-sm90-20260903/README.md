# H100 SM90 GPU-AMR historical evidence

This directory records the qualification run begun on 2026-09-03 and closed on
2026-09-05. JSON files are the
machine-readable authority; Markdown summarizes them without replacing them.

## Audit correction: not final-artifact qualification

The three archived AMR/restart JSON reports all record `binary_sha256` as
`d42711fa4a19357713be49df77cd3ab714faf4ccfe89c5d9bb16efa09231490b`.
The last linked executable in `final-artifacts.sha256` instead records
`e903232e298ee9ea2cc958a404fe370a305463c8008ec5969b4adb9430f4f8e2`.
Therefore the successful matrix/restart results below apply to their earlier
recorded binary, **not** the final linked artifact, nor the later shared-math
and Release-floating-point fixes. The historical build/test logs remain useful
but do not close that gap. All raw JSON, hashes and logs are preserved unchanged.

Current qualification requires fresh Debug and Release final artifacts, each
with a complete matrix and both restart suites, using the required `--build-dir`
and the same source/build identity and application/comparator hashes throughout.
Run `tools/qualify_cuda_amr_evidence.py` after the runtime validators as described
in [the current AMR qualification procedure](../../README.md#final-artifact-qualification).
It rejects the mismatch above and also rejects historical reports with matching
application hashes but incomplete provenance. Its checked-in regression is
`python3 -B tests/test_validation_provenance.py` (run from the repository root).
New focused tests do not mean the complete historical workload has been rerun.

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
| compensated backend rebuild | pass | `1:13:21` | `4,752,228 KiB` | `0` |
| final incremental `ARCH` link | pass | `1:06.81` | `1,206,036 KiB` | `0` |

Artifacts:

- `libarch_cuda_backend.a`: 176,691,142 bytes, SHA-256
  `9fa494d579c5450265789b19a16e83c4c2ba380785980a4fef8a11a9f81a4eb8`;
- `ARCH`: 204,248,832 bytes, SHA-256
  `e903232e298ee9ea2cc958a404fe370a305463c8008ec5969b4adb9430f4f8e2`.

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

The first full CTest run passed 52 of 65 tests while unrelated jobs occupied
about 9.4 GiB of the 20 GiB vGPU. `ctest-initial.log` is retained unchanged.
The first resource-isolated retry passed 10/13: eliminating the allocation
pressure exposed three deterministic burn CPU/CUDA precision failures. The
production reductions formerly accumulated on the Host as `long double` while
CUDA treated device `long double` as `double`. They now share a fixed-order
compensated `double` implementation for Ye, Timmes RHS/Jacobian and
temperature-energy terms, ODE energy closure, and NSE conservation totals.
The remaining aprox19 ROS4 transcendental difference uses per-field budgets at
125% of the measured maxima; no suite-wide tolerance was relaxed.

After the fix, burn-policy parity passes 16/16, the original focused set passes
13/13, and the full CTest suite passes 65/65. Tooling tests pass 72/72. See
`ctest-resource-isolated-first.log`, `burn-policy-parity-final.log`,
`ctest-focused-13-final.log`, `ctest-full-final.log`, and `tooling-final.log`.

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
- `ctest-resource-isolated-first.log`: first isolated retry that exposed the
  deterministic burn precision defect;
- `burn-policy-parity-final.log`: final 16/16 burn parity result;
- `ctest-focused-13-final.log`: final retry of the original 13 failures;
- `ctest-full-final.log`: final full-suite 65/65 result;
- `tooling-final.log`: final 72/72 tooling result and combination audit;
- `cuda-launch-blocking.log`: synchronous-launch focused regression;
- `arch-gpu-amr-final-*-time.txt`: raw build resource records;
- `compensated-burn-build-time.log`: backend rebuild after compensated sums;
- `compensated-burn-budget-build-time.log`: final burn parity TU rebuild;
- `compensated-ARCH-link-time.log`: final executable relink record;
- `final-artifacts.sha256`: hashes of the final archive and executable;
- `run-resource-isolated-ctest.sh`: resource-isolated rerun command;
- `tu-memory-samples.csv`: per-source sampled build-scope memory peaks;
- `arch-gpu-amr-final-sanitizer-*.log`: unsupported sanitizer attempts.
