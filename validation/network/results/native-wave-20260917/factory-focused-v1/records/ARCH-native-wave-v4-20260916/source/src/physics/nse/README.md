# Network-constrained nuclear statistical equilibrium

[nse_solver.h](nse_solver.h) owns the shared equilibrium solver, including
chemical-potential iteration, mass/charge constraints and certification of an
already equilibrated input. The selected supported network supplies its species
and nuclear data.

The CPU and CUDA backends share the same solver. The burn layer owns
temperature/energy coupling and acceptance of an NSE handoff; device execution
and memory ownership remain backend concerns. The result is equilibrium over
the selected species, not an unrestricted nuclear-species catalog.

Built-in aprox/iso networks retain their Timmes data convention. Eligible
generated packages provide their own masses, binding energies, spin weights
and data conventions to this same solver. Their projection energy uses
`physics/network/NuclearEnergy.h`, the same accepted-increment contraction as
ordinary ODE burning. Eligibility and unsupported-model reasons belong to the
[network generator](../../../tools/network/README.md), not to backend switches.

`use_nse=true` requires an eligible network, `false` disables the bypass, and
`auto` selects availability at startup. Once enabled, both true and auto use
exactly `T > nseTempThreshold` and `rho > nseDensThreshold`; defaults remain
`4.5e9 K` and `1e6 g/cm^3`. Auto does not add a timescale or composition-distance
threshold. Data-domain, mass/charge and coupled-energy checks still apply in
both modes. A failed projection leaves the accepted state intact and returns
to the ordinary ODE path, rather than clamping temperature into equilibrium.

The solver is adapted from Frank Timmes's public NSE work; retain its source
attribution and [third-party notices](../../../THIRD_PARTY_NOTICES.md).

See the [NSE discussion](../../../docs/physics/TimmesNetworks.md),
[Reference](../../../docs/Reference.md) and
[burn validation](../../../validation/burn/README.md).
