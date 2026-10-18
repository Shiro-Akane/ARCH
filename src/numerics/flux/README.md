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

HLLC's identical-state path can borrow `MeanThermoView`, the same read-only SoA
view on host and device. The stage owner publishes required mean P/c first;
reuse checks every conserved component and species against one owning mean.
Nonidentical endpoints also require the exact EOS input energy to match before
borrowing mean thermodynamics; equal conserved components can recover a
differently rounded energy in a directional calculation. Changed inputs evaluate
the selected EOS. This preserves the shared flux identity and removes the former host-container dependency from this path.

[FluxSweep.h](FluxSweep.h) owns the single CPU face traversal, stage-mean
preparation and conservative limiting shared by every flux. CUDA binds resident
arrays to the same mean view and face mathematics. `hll_wave_speed=roe|davis`
selects the signal estimate for HLL and HLLC with any supported reconstruction;
for those two solvers the selection is required and no compatibility default is
restored, so a missing or invalid value is rejected. `roe` uses the
Roe–Glaister acoustic estimate and `davis` uses endpoint acoustic bounds. Every
route uses the physical face EOS; approximate face closure is not a supported
policy.

[StationarySlipWallFlux.h](StationarySlipWallFlux.h) owns the shared stationary
slip-wall point law and its selected baseline. An EOS with the explicit
`roe_gamma_minus_one` capability keeps the exact gamma-law reflected-wave
pressure. Other EOS use the actual interior point pressure as a consistent
numerical wall traction; this is not an exact general-EOS Riemann solution.
Mass, energy, tangential momentum and species advection are zero. Reconstructed
high and immutable mean low use their own actual EOS data and the original two
bar-state admission with one limiter factor. A bad required baseline still
rejects; neither effective gamma, a pressure clamp nor an increased acoustic
speed supplies a replacement. Host and Device traversals consume this
same owner; native face quadrature and its common factor remain unchanged.

The physical-pressure boundary form follows the stationary-wall construction
in [Hindenlang, Gassner and Kopriva](https://arxiv.org/abs/1901.04924). The paper's
ideal-gas DG analysis is not a proof for an entire general-EOS limited FV stage.
The current owner tests use independent stiffened-gas reflected-wave integrals;
the release plan separately records actual Helm/Cellular application evidence.
