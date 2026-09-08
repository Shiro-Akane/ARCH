# Local shared-mathematics review fixes (2026-09-05)

These are bounded follow-up checks of the working-tree fixes above `2226456`,
not replacement H100 qualification reports or final ARCH acceptance.

## Environment

- WSL Linux, approximately 7.7 GiB available guest RAM capacity, 2 GiB swap;
  build monitoring stops before available RAM falls below 1,800 MiB or swap
  use exceeds 32 MiB.
- NVIDIA GeForce RTX 3060 Ti, 8,192 MiB VRAM, driver 610.47.
- CUDA 12.3.107, GCC 12.4.0, CMake 3.28.3, SM86, serial builds.
- `ARCH_ENABLE_KLU=OFF`; ordinary CPU Debug and CUDA Debug/Release configured
  in separate `build/review-*` trees. All use the shared strict-FP contract.

## Completed bounded checks

| Check | Result | Recorded output |
| --- | --- | --- |
| CPU Debug CTest, including frozen-main burn references | 21/21 passed | `debug-cpu-ctest.log` |
| Release CPU compensation, burn references, AMR plans, and two CUDA mathematical tests | 5/5 passed | `release-focused-ctest.log` |
| Debug CUDA compensation and production AMR composition kernels on SM86 | 2/2 passed; no skips | `debug-cuda-focused-ctest.log` |
| Debug GPU burn policy helpers, status/retry checks, and twelve network/solver routes with frozen-main gates | 16/16 passed in 79.90 s; unchanged parity budgets | `debug-burn-policy-ctest.log` |
| Debug/Release compensation and AMR kernels under memcheck and racecheck | 8/8 runs passed; zero reported errors/leaks/hazards | `{debug,release}-{compensation,amr}-{memcheck,racecheck}.log` |
| Release compensation with externally injected `CMAKE_CXX_FLAGS=-ffast-math` | 1/1 passed, including subnormal preservation | `injected-fast-math-ctest.log` |
| Python validator, provenance, EOS runtime inputs, and combination-audit contracts | 119/119 passed | Command below; overlaps three CPU CTest entries |
| `tools/audit_combination_v2.py` and `git diff --check` | Both exit 0 | No findings |

The immutable burn reference test covers 12 network/solver routes and 72
negative controls. The Python command was:

~~~bash
python3 -B -m unittest tests/test_validate_backend_results.py \
  tests/test_validation_provenance.py tests/test_runtime_validation_inputs.py \
  tests/tooling/test_audit_combination_v2.py
~~~

The guarded complete Debug backend build was **interrupted intentionally**
after 867.5 seconds to prioritize the changed regression targets. It completed
the `CudaBackendBurnIdeal.cu` object but not the archive; this is neither a
compiler failure nor a successful full build. The observed minimum available
Host RAM was 3,571 MiB and maximum swap usage was zero. The partial compiler
output is preserved in `debug-backend-interrupted-build.log`. Completed objects
remain in `build/review-cuda-debug`; no compiler processes were left running
after interruption.

The focused `arch_cuda_burn_policy_parity` target subsequently built and linked
through normal CMake in 831.3 seconds. It reuses the three existing production
Helmholtz/species/utility owner OBJECT targets, not a substitute backend or a
second implementation. Minimum observed available RAM was 1,858 MiB; the guard
did not fire. Its whole-MiB swap counter reported zero, but a finer `/proc`
snapshot showed 524 KiB in use. Accordingly this is **not** a zero-swap capacity
qualification. See `debug-burn-policy-build.log` for compiler output. The final
incremental target build reported `ninja: no work to do`.

## What the checks establish

- The CPU regression suite includes the shared compensated sum and AMR
  operation plans, table EOS, restart compatibility and independent burn
  references extracted from frozen main `8c76be85…`.
- The lightweight CUDA tests execute the actual production coarse/fine
  gather/scatter kernels, not copied mathematical implementations. They cover
  four species in 1/2/3D, zero/trace species, linear/fallback family conservation
  and whole-plan rejection for zero/negative/NaN/infinite density.
- Host/device compensation uses independent exact cancellation cases,
  including a subnormal value; Host terms are staged through volatile test
  input memory so Release/LTO cannot precompute the answer.
- The changed production exchange/control objects and full-backend AMR test
  source are separately compiled using the generated CMake commands.

## Limits and required follow-up

- Final complete ARCH Debug/Release builds, full CUDA CTest, production AMR
  and both restart reruns must be recorded against the same final artifacts.
  The three old H100 reports cannot satisfy this gate (different executable).
  The focused GPU burn policy test was built/run in Debug only; its Release
  rerun is also outstanding (the Release host burn-reference test did pass).
- The initial local Compute Sanitizer failure is retained in
  `debug-amr-memcheck-unavailable.log`. A read-only Windows registry query
  found the NVIDIA `GPUDebugger/EnableInterface` value absent. The user then
  explicitly enabled it as DWORD 1 from administrator PowerShell; the eight
  focused Debug/Release sanitizer runs passed immediately without a reboot.
  No driver installation, TDR change, or automatic administrator mutation was
  performed by the agent. These focused results do not qualify the complete
  ARCH runtime, dynamic topology transactions, burn, or restart under sanitizer.
- No claim is made about a sustained representative 16 GiB runtime workload,
  deeper AMR levels, performance, `--parallel 6`, or deferred CUDA providers.
- The pre-existing untracked `simulation/CooperativeHotspots` files were not
  edited. Its `.cpp` is an actual CMake source input and therefore must be
  included in the source identity if a full build uses this worktree.

See `docs/CudaBackendStatus.md` and `validation/amr/README.md` for the complete
qualification commands and the distinction between source fixes and final
runtime acceptance. Raw bounded test logs, when present here, are not final
production matrix evidence.
