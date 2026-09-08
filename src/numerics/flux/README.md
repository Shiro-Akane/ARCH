# Hydrodynamic flux policies

These headers turn reconstructed face states and an EOS policy into conservative
hydrodynamic and species fluxes.

[FluxFunctions.h](FluxFunctions.h) owns common directional transforms,
physical-flux operations and thermodynamic averaging. The selectable policies
are [HLL](FluxHLL.h), [HLLC](FluxHLLC.h), [Roe](FluxRoe.h),
[Steger–Warming](FluxSW.h) and [Van Leer](FluxVL.h).

Each flux policy is defined by a single mathematical body that is shared between CPU and CUDA instantiations. The reconstruction step supplies the input states, while the EOS handles thermodynamic queries. The backend kernels are solely responsible for executing face traversals and storing the resulting fluxes. Finally, policy selection is strictly a function of the common dispatch layer, rather than being hardcoded into these numerical formulas.

See the [Reference](../../../docs/Reference.md) for the policy interfaces and
[hydro validation](../../../validation/hydro/README.md) for their checks.
