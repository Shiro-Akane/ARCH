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

CPU execution services continuation requests directly. CUDA keeps continuation
state on device and services sparse requests through its provider adapter.
Neither executor owns another reaction formula or acceptance rule.

See the [Reference](../../../docs/Reference.md),
[burn validation](../../../validation/burn/README.md) and
[network validation](../../../validation/network/README.md).
