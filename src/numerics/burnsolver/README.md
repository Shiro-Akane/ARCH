# Burn integration

This directory couples reaction networks, thermodynamics and linear solvers
without introducing separate CPU and CUDA ODE algorithms.

- [OdeContinuation.h](ode/OdeContinuation.h) defines shared linear-solve suspension
  points and workspace contracts.
- [ode_be-nr.h](ode/ode_be-nr.h), [ode_bd.h](ode/ode_bd.h) and
  [ode_ros4.h](ode/ode_ros4.h) own each method's continuation and adaptive control.
- [odeFunction.h](coupling/odeFunction.h) owns common RHS/Jacobian assembly, accepted-state
  arithmetic, accepted energy and NSE handoff; [BurnThermodynamics.h](coupling/BurnThermodynamics.h)
  supplies the shared first-law closure.
- [NetworkDerivative.h](coupling/NetworkDerivative.h) supplies the generated-network
  temperature-derivative fallback. [BurnDispatch.h](BurnDispatch.h),
  [BurnerHandle.h](BurnerHandle.h) and [Networks.h](Networks.h) bind policies.

Burn dispatch consumes resolved network, ODE and linear-solver IDs. Names,
aliases and backend support are handled by the
[shared resolver](../../driver/dispatch/README.md) before factory construction.

During execution, the CPU services ODE continuation requests directly. CUDA
keeps continuation state on the device and services sparse requests through its
provider adapter. Both executors use the shared reaction formulas and
thermodynamic acceptance rules. Generated networks can reuse the RHS already
formed during Jacobian assembly; Helm supplies one derivative snapshot for that
assembly. Neither reuse crosses an ODE trial boundary.

The host scheduler batches cell ranges and can reuse successful results only
for bit-identical complete inputs within one burn half-step. Custom policies
must explicitly opt into that pure-input contract. Both Strang half-steps,
energy handoff and per-cell admissibility checks remain active. Table EOS
queries for uncommitted ODE/NSE candidates use a local failure latch so the
existing retry or line search can recover. Required initial and accepted-state
queries keep strict failure propagation.

See the [Reference](../../../docs/Reference.md),
[burn validation](../../../validation/burn/README.md) and
[network validation](../../../validation/network/README.md).
