# Network-constrained nuclear statistical equilibrium

[nse_solver.h](nse_solver.h) owns the shared equilibrium solver, including
chemical-potential iteration, mass/charge constraints and certification of an
already equilibrated input. The selected supported network supplies its species
and nuclear data.

CPU and CUDA use this same solver. The burn layer owns temperature/energy
coupling and acceptance of an NSE handoff; device execution and memory ownership
remain backend concerns. This is equilibrium over the selected network, not an
unrestricted nuclear-species catalogue or a generated-network NSE promise.

The solver is adapted from Frank Timmes's public NSE work; retain its source
attribution and [third-party notices](../../../THIRD_PARTY_NOTICES.md).

See the [NSE discussion](../../../docs/physics/TimmesNetworks.md),
[Reference](../../../docs/Reference.md) and
[burn validation](../../../validation/burn/README.md).
