# Bounded audit150 / audit200 follow-up

User authorization (2026-09-09) extends the previous owner-approved local-machine
deferral to this 114 GiB RAM / 20 GiB H100-20C server. No production mathematics,
EOS, generated recipe, ODE tolerance or provider policy is changed.

Use the committed audit150/audit200 recipes and pynucastro 2.12.0 in isolated
`build/network-python-20260909`. The optional provider is cuDSS 0.8.0.10;
SuiteSparse KLU uses the project's pinned v7.13.0. Retain the full resolved
Python package versions, generated manifests, commands, build provenance and
per-compiler-invocation maximum child RSS. This RSS is not aggregate system RSS.
Generated packages are copied unchanged to the external audit root
`/home/ubuntu/projects/ARCH-large-networks-20260909`, as required by CMake's
custom-network source isolation contract.
Use the existing pressure guard to protect at least 16 GiB available memory and
allow no additional swap growth.

1. Compile the original generated-math tests, cuDSS provider test and both real
   generated sparse-burn typed factories (all three ODEs). No substitute matrix
   or reduced nuclear network is permitted.
2. Run the original sparse validation tool for each network at rho=1e7,
   T=3e9, cv=1e8, interval=1e-10, rtol=1e-7, four external steps and
   X(C12)=X(O16)=0.5. Preserve the 2e-10 field and 2e-8 limiter budgets,
   two-/three-cell storage change and bounded provider reuse.
3. If the focused matrix passes, extend physical trajectory duration and
   storage/capacity coverage with explicit new controls and unchanged budgets.
   These diagnostic timings do not enter the Sedov speedup comparison.
4. Full ARCH/EOS integration and large-capacity scaling are separate from focused
   factory parity. Do not label these complete without their own successful
   build/run evidence. Independent physical-oracle accuracy is likewise separate.
5. Attempt device instrumentation only if the vGPU permits it; record any
   platform restriction as unverified, never as a sanitizer pass.

Heavy compilation, network generation and cuDSS runs must not overlap the
uninstrumented CPU/GPU Sedov timing window. The benchmark uses its own unchanged
Release configuration and remains independent of this optional-provider build.

## Current checkpoint

- Both original recipes generated: audit150 = 150 nuclei / 1416 rates;
  audit200 = 200 nuclei / 1965 rates. Original recipes and species counts retained.
- cuDSS 0.8.0.10 installed in the isolated environment. The original toolchain
  has no cuBLAS package; configuration explicitly binds the isolated dependency
  cuBLAS 12.9.2.10. The failed discovery log is preserved. No global upgrade.
- CMake now registers both custom CUDA networks, KLU and cuDSS successfully.
- `cuda_cudss_sparse_solver` passed on the H100-20C (1.37 s). This generic
  provider witness is not a real audit150/audit200 trajectory result.
- The uncontended Sedov timing/profile window completed and passed. Focused
  network compilation is active; trajectories follow it without lowering budgets.
- Full NVIDIA sanitizer 12.8.93 is installed separately after the PyPI package
  was found to omit TreeLauncherSubreaper. Real instrumentation is blocked by
  the H100 vGPU: `GPU debugging features are disabled`, error summary 1.
  Device safety is unverified, not passed. No driver/security settings changed.

## Explicit follow-up controls (test-only extension, not yet executed)

The upstream typed-factory harness hard-codes storage 2/3 and requested pool 2.
The local test-only patch parameterizes those extents, preserves the default
transcript schema and budgets, and requires the runner to verify the requested
extents against an explicit transcript record. The production owner still
selects capacity from requested size, hardware warp width and available memory.
No production source or generated mathematics is changed. The original pristine
four-step matrix must finish before transferring or rebuilding this patch.

After a successful original matrix, recheck the default 2/3 case, then test
32/33 cells with requested pool 32, 128/129 cells with pool 32, and a longer
40-step trajectory over 1e-8 using the default 2/3 cells. The density variation
remains the existing rho*(1+0.05*cell); the largest cell therefore reaches 7.4e7.
These are capacity/trajectory checks, not a matched eight-thread CPU speed test.
Record actual pool capacity and observed GPU memory separately from requested
capacity and calculated lane bytes; opaque cuDSS factor memory is not included
in the latter. The provider retains one factor set rather than one per cell.
Each case has a 2400-second budget. Stop increasing a network's workload at the
first failure, retain logs, and do not loosen budgets or infer a pass from timeout.

Parser verification: nine tests passed locally, including the original archived
audit31 transcript and negative extent/identity/capacity/coverage/budget controls.
The unchanged upstream validation-tool subset passed 73/73 on Linux. A broader
Windows run of 80 tests had five errors in unrelated existing tests (symlink
privilege, SQLite file cleanup, Git executable discovery, canonical path identity,
and locale-dependent subprocess decoding); no production repair was attempted
for these host-specific failures. Repeat relevant contracts on Linux after patching.

## Full application compilation (separate pristine worktree)

`/home/ubuntu/projects/ARCH-large-integration-20260909` is a detached worktree at
the same upstream `0266d96f20b184d4b17ebc6a066ac3b9021f1642`. Its Release build
registers both original large networks and all production EOS routes, with KLU
and cuDSS enabled. BUILD_TESTING=OFF excludes test executables, not production
owners. The ARCH target must build the complete CUDA archive and link; no
failing EOS/network translation unit may be skipped. Source/status checks guard
this pristine identity independently from the focused harness's later patch.

The full build uses four heavy / six total jobs, plus the two-job focused build
already running, with a stronger 32 GiB available-memory floor and zero new
swap allowed. This is a distinct, recorded parallel build configuration, not
a directly comparable isolated compile-time measurement. Reused HighFive and
pinned SuiteSparse sources are read-only; each build owns its own outputs.
The completed Sedov timing/profile window is unaffected by this later work.
