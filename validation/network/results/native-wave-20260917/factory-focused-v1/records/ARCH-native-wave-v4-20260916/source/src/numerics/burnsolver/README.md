# Burn integration

This directory couples reaction networks, thermodynamics and linear solvers
without introducing separate CPU and CUDA ODE algorithms.

- [OdeContinuation.h](OdeContinuation.h) defines shared linear-solve suspension
  points and workspace contracts.
- [ode_be-nr.h](ode_be-nr.h), [ode_bd.h](ode_bd.h) and
  [ode_ros4.h](ode_ros4.h) own each method's continuation and adaptive control.
- [odeFunction.h](odeFunction.h) owns common RHS/Jacobian assembly, accepted-state
  arithmetic, accepted energy and NSE handoff; [BurnThermodynamics.h](BurnThermodynamics.h)
  supplies the shared first-law closure.
- [NetworkDerivative.h](NetworkDerivative.h) supplies the generated-network
  temperature-derivative fallback. [BurnDispatch.h](BurnDispatch.h),
  [BurnerHandle.h](BurnerHandle.h) and [Networks.h](Networks.h) bind policies.

Burn dispatch consumes resolved network, ODE and linear-solver IDs. Names,
aliases and backend support are handled by the
[shared resolver](../../driver/dispatch/README.md) before factory construction.

During execution, the CPU services ODE continuation requests directly. In contrast, CUDA maintains the continuation state directly on the device and services sparse requests exclusively through its provider adapter. Importantly, neither executor implements an alternative reaction formula or thermodynamic acceptance rule; both defer to the shared numerics.

See the [Reference](../../../docs/Reference.md),
[burn validation](../../../validation/burn/README.md) and
[network validation](../../../validation/network/README.md).
