# Linear algebra policies

This directory separates matrix structure and numerical operations from the
external provider used to factor and solve a system.

- [DenseWrap.h](DenseWrap.h) owns compact dense matrix operations and DenseLU.
- [SparseWrap.h](SparseWrap.h) owns the CPU sparse matrix/KLU policy.
- [CsrPattern.h](CsrPattern.h) constructs host-side symbolic CSR structure;
  [CsrMatrixView.h](CsrMatrixView.h) exposes backend-neutral numerical entries.
- [LinearEquilibration.h](LinearEquilibration.h) owns shared equation/unknown
  scaling operations.

The burn layer supplies the same Jacobian and continuation to these policies.
CUDA's cuDSS adapter owns device buffers, native handles and factor lifetimes;
its kernels reuse the shared scaling math. KLU and cuDSS remain separate
providers, not separate ODE implementations. Selection and equation-count
limits belong to the common dispatch contract.

See the [Reference](../../../docs/Reference.md),
[network validation](../../../validation/network/README.md) and
[third-party notices](../../../THIRD_PARTY_NOTICES.md).
