# Network-constrained nuclear statistical equilibrium

[nse_solver.h](nse_solver.h) owns the shared equilibrium solver, including
chemical-potential iteration, mass/charge constraints and certification of an
already equilibrated input. The selected supported network supplies its species
and nuclear data.

The CPU and CUDA backends share this exact same solver implementation. The burn layer exclusively owns temperature/energy coupling as well as the acceptance of an NSE handoff, while device execution and memory ownership remain purely backend concerns. It is important to emphasize that this implementation calculates equilibrium only over the specific species included in the selected network—it is not an unrestricted nuclear-species catalog, nor does it imply an NSE promise for externally generated networks.

The solver is adapted from Frank Timmes's public NSE work; retain its source
attribution and [third-party notices](../../../THIRD_PARTY_NOTICES.md).

See the [NSE discussion](../../../docs/physics/TimmesNetworks.md),
[Reference](../../../docs/Reference.md) and
[burn validation](../../../validation/burn/README.md).
