# Linear algebra policies

This directory separates matrix structure and numerical operations from the
external provider used to factor and solve a system.

- [DenseWrap.h](DenseWrap.h) owns compact dense matrix operations and DenseLU.
- [SparseWrap.h](SparseWrap.h) owns the CPU sparse matrix/KLU policy.
- [CsrPattern.h](CsrPattern.h) constructs host-side symbolic CSR structure;
  [CsrMatrixView.h](CsrMatrixView.h) exposes backend-neutral numerical entries.
- [LinearEquilibration.h](LinearEquilibration.h) owns shared equation/unknown
  scaling operations.

The burn layer feeds the exact same Jacobian and continuation logic into all these policies. For GPU execution, CUDA's cuDSS adapter manages device buffers, native handles, and factor lifetimes, while its kernels directly reuse the shared scaling math. It is important to emphasize that KLU and cuDSS serve merely as distinct linear-solver providers, not as separate ODE implementations. The rules for provider selection and equation-count limits are enforced by the common dispatch contract.

See the [Reference](../../../docs/Reference.md),
[network validation](../../../validation/network/README.md) and
[third-party notices](../../../THIRD_PARTY_NOTICES.md).
