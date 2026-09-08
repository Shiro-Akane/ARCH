# Physical models

This tree owns physical closures, material properties and reaction data consumed
by the [numerical methods](../numerics/README.md).

- [EOS](eos/README.md): thermodynamic queries and table ownership.
- [Networks](network/README.md) and [NSE](nse/README.md): reaction physics and
  equilibrium over a supported network's species.
- [Species](species/README.md): composition metadata and shared mixture queries.
- [Gravity](gravity/README.md): the configured gravitational source.
- [Diffusion coefficients](diffusionCoe/README.md): stellar conductivity.
- [Diagnostics](diagnostics/README.md): metric-aware velocity diagnostics.
- [Constants and units](constant/README.md): project-owned constant definitions.

Each physical model has one mathematical authority used by both backends.
Host owners and CUDA owners may differ in allocation and table placement;
borrowed views expose those data to the same formulas. Numerical integration,
mesh traversal and runtime dispatch belong to their respective modules.

See the [Reference](../../docs/Reference.md),
[validation overview](../../validation/README.md) and
[third-party notices](../../THIRD_PARTY_NOTICES.md).
