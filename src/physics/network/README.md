# Reaction-network physics

This directory supplies network species, reaction rates, RHS evaluations,
derivatives and nuclear-energy data. Time integration belongs to
[burnsolver](../../numerics/burnsolver/README.md), not to a network backend.

- Maintained built-ins: [iso7](iso7/README.md), [aprox13](aprox13/README.md),
  [aprox19](aprox19/README.md) and [aprox21](aprox21/README.md).
- [timmes_common](timmes_common/README.md) contains their shared support.
- [custom](custom/README.md) documents generated-package ownership and registration.
- [WeakTableView.h](WeakTableView.h) provides borrowed immutable weak-table views.

CPU and CUDA instantiate the same built-in or generated mathematical bodies.
Backend owners provide data lifetime and device placement; factories bind the
selected network without copying reaction formulas. Network data retain their
own provenance, distinct from project-wide constants.

See the [Reference](../../../docs/Reference.md),
[Timmes network guide](../../../docs/physics/TimmesNetworks.md),
[network validation](../../../validation/network/README.md) and
[third-party notices](../../../THIRD_PARTY_NOTICES.md).
