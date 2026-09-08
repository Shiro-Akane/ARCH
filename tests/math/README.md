# Shared numerical witnesses

These headers define reusable test cases for CPU and CUDA executors. Analytic
identities, manufactured states and independently derived reference data check
the production routines without maintaining another solver.

- [BurnThermalCases.h](BurnThermalCases.h): thermal closure, Jacobian and rejected
  ODE-trial checks.
- [DenseLuCases.h](DenseLuCases.h) and [NetworkDerivativeCases.h](NetworkDerivativeCases.h):
  mixed-scale linear solves and derivative callbacks.
- [CompensatedSumCases.h](CompensatedSumCases.h): cancellation-sensitive sums.
- [CurvilinearMetricCases.h](CurvilinearMetricCases.h) and
  [ViscousGeometryCases.h](ViscousGeometryCases.h): physical measures and analytic
  diffusion witnesses.
- [RoeThermodynamicCases.h](RoeThermodynamicCases.h): thermodynamic and flux identities.

Data-only endpoint references live in [fixtures/](../fixtures/README.md). Keep
backend allocation and launches in their test executors, and retain each
reference's derivation and tolerance when extending a case.
