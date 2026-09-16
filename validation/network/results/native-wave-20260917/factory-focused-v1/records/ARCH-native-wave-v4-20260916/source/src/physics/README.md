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

Each physical model is defined by a single mathematical authority that is shared by both backends. While host owners and CUDA owners may manage memory allocation and table placement differently, they expose their data to these shared formulas using borrowed views. Furthermore, numerical integration, mesh traversal, and runtime dispatch logic are purposefully excluded from these models and belong entirely to their respective modules.

See the [Reference](../../docs/Reference.md),
[validation overview](../../validation/README.md) and
[third-party notices](../../THIRD_PARTY_NOTICES.md).
