# Hydrodynamic flux policies

These headers turn reconstructed face states and an EOS policy into conservative
hydrodynamic and species fluxes.

[FluxFunctions.h](FluxFunctions.h) owns common directional transforms,
physical-flux operations and thermodynamic averaging. The selectable policies
are [HLL](FluxHLL.h), [HLLC](FluxHLLC.h), [Roe](FluxRoe.h),
[Steger–Warming](FluxSW.h) and [Van Leer](FluxVL.h).

Each policy has one mathematical body shared by CPU and CUDA instantiations.
Reconstruction supplies its input states; the EOS supplies thermodynamic
queries. Backend kernels perform face traversal and store the resulting fluxes.
Policy selection belongs to the common dispatch layer, not to these formulas.

See the [Reference](../../../docs/Reference.md) for the policy interfaces and
[hydro validation](../../../validation/hydro/README.md) for their checks.
