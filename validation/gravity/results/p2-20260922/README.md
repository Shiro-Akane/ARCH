# P2 standalone CPU Poisson / multigrid — 2026-09-22

Base: P1.5 `780794b69afb118249d9bed9de44c547fcedd222`, already pushed to
`physics/selfgravity`. This record covers the single-domain mathematical
prototype; it does **not** enable production `gravity_type=self`.

The [decision record](../../../../docs/development/P2PoissonMultigrid.zh-CN.md)
fixes the discretization, internal MG choices and numerical budgets. The
[main plan](../../../../docs/development/SelfGravityImplementationPlan.zh-CN.md)
retains P3 AMR, P4 hydro/energy coupling and P6 device execution as separate gates.

## Implemented and measured

- Uniform Cartesian 1D/2D/3D; fully periodic or prescribed-potential Dirichlet.
- Matrix-free negative Laplacian, second-order face boundary gradients,
  rediscretized V-cycle, signed transfer and reuse of the existing dense LU on
  at most 64 coarse unknowns. No new required sparse-provider dependency.
- Reusable CPU workspace; the physics adapter samples a borrowed Host scalar
  view and returns CGS potential plus face/cell acceleration only on success.
- No physical configuration keys or GUI schemas changed. No density floor or
  fixed epsilon was added. `rtol/atol/max_cycles` belong to the mathematical
  solve contract; smoother choices remain internal.

The [Release numerical record](cpu-release/report.json) names the candidate
source files and exact executable. It uses the Host-only test executable from
an existing CUDA-enabled Release build; **no GPU kernel executes in these tests**.
The archive helper reuses shared provenance and process/logging utilities and
checks that the named source and executable identities remain unchanged.

| Reference, 16 → 32 → 64 cells per axis | Measured order range |
|---|---:|
| Periodic potential | 2.0021–2.0084 |
| Periodic cell acceleration | 2.0007–2.0028 |
| Periodic face acceleration | 1.9833–1.9902 |
| Dirichlet potential | 1.8522–1.9293 |
| Dirichlet cell acceleration | 1.9750–1.9963 |
| Dirichlet face acceleration, all faces | 2.0059–2.0573 |
| Dirichlet boundary faces separately | 1.9970–2.0100 |

All exceed the frozen **1.8** target. The 18 solves took **10–20 V-cycles**;
see [convergence.csv](cpu-release/convergence.csv). Nonzero Dirichlet data enter
the effective RHS norm, which changes with grid spacing; these cycle counts
alone are not a universal performance or mesh-independence claim.

The six [CGS sinusoidal density cases](cpu-release/cgs.csv) use density scales
`1`, `1e-30`, `1e-100 g/cm^3` and domain lengths `1`, `1e6 cm`. At 32 cells,
normalized potential RMS error is **0.3219%** and combined face/cell acceleration
RMS error is **0.2523% or less**, both below the frozen 1% budget. Normalization
uses the continuum analytic amplitude divided by sqrt(2). Rescaled potentials
agree within `1e-10`. Constant-density quadratic-potential cases also pass in
all three dimensions; these prescribed boundaries are not isolated gravity.

Contract checks additionally cover independent discrete Fourier eigenvalues,
constant/linear/quadratic fields, signed/linear transfers, residual recomputation,
zero source, warm starts, repeated solves, malformed geometry/BC/views, NaN/Inf,
arithmetic underflow/overflow, incompatible periodic source and cycle-limit
failure with no returned fields. A shifted `1e-10` density contrast exposed
mean-subtraction roundoff during development:
[pre-fix rejection](integration/small-contrast-before-fix.txt). The physics
adapter now records an additional source-mean projection. The generic solver's
strict compatibility gate and all analytic budgets remain unchanged.

## Build, regression and instrumentation

- CPU Debug application and all targets compile; CUDA-enabled Release application
  and the new Host target compile. See [CPU](compatibility/cpu-build.txt) and
  [CUDA build](compatibility/cuda-build.txt) logs.
- Full CPU CTest result: **57/57 passed, zero skips**, 470.42 s. Its inventory (whitespace-normalized JSON with unchanged content), JUnit and raw output are in
  [integration/](integration/). New P2 tests are also required CI coverage anchors.
- The CI result checker passes its existing **10** tool tests; the architecture
  audit and `-Wall -Wextra -Wpedantic` check complete with no diagnostics.
- AddressSanitizer + UndefinedBehaviorSanitizer with leak detection pass both
  contract and analytic modes, including the 64^3 case. See
  [contract](sanitizer/contract-direct.txt) and [analytic](sanitizer/analytic-direct.txt).
  The first sandboxed attempt could not run LeakSanitizer under ptrace
  ([environment log](sanitizer/contract.txt)); the successful direct runs keep
  `detect_leaks=1`, `halt_on_error=1` and the same test binary.
- CUDA 12.3 / sm_86 compiles the shared stencil and transfer leaves with the
  existing strict floating-point flags and relaxed constexpr setting; see
  [compile-only probe](compatibility/poisson_leaves.cu). This checks compilation,
  not device correctness, residency or acceleration.

Exact commands, selected binary identities and tool versions are recorded in
[verification.json](verification.json). Existing P1.5 scientific campaigns are
retained under their original identities; this new additive prototype does not
replace them. The production capability regression continues to reject self.

## Reproduce

From the repository root, use an ordinary testing-enabled CPU build:

```bash
cmake --build build --target ARCH arch_poisson_multigrid --parallel 4
ctest --test-dir build -R '^poisson_multigrid_' --output-on-failure
python3 validation/gravity/p2_cpu_poisson.py \
  --build-dir build --output-dir /tmp/arch-poisson-new
```

The evidence output directory must be empty. Full CPU regression additionally
uses the existing CTest inventory/JUnit completion checker; it rejects skips or
missing tests. No FFT, isolated-boundary model, AMR composite solve, production
field publication, energy coupling or GPU gravity was added in this phase.
