# Shared numerical witnesses

These headers define reusable test cases for CPU and CUDA executors. Analytic
identities, manufactured states and independently derived reference data check
the production routines without maintaining another solver.

Here a "witness" means a specific input and its expected numerical property,
such as a known derivative or conserved quantity. Sharing these cases lets the
host and device tests ask the same question of the same mathematical routine.

- [BurnThermalCases.h](BurnThermalCases.h): thermal closure, Jacobian and rejected
  ODE-trial checks.
- [DenseLuCases.h](DenseLuCases.h) and [NetworkDerivativeCases.h](NetworkDerivativeCases.h):
  mixed-scale linear solves and derivative callbacks.
- [CompensatedSumCases.h](CompensatedSumCases.h): cancellation-sensitive sums.
- [GeneratedNseCases.h](GeneratedNseCases.h): independent rank-one/rank-two
  equilibrium, fixed points, energy-gauge/thermal closure and rejection controls.
- [CurvilinearMetricCases.h](CurvilinearMetricCases.h) and
  [ViscousGeometryCases.h](ViscousGeometryCases.h): physical measures and analytic
  diffusion witnesses.
- [RoeThermodynamicCases.h](RoeThermodynamicCases.h): thermodynamic and flux identities.
- [Strict tabular inversion](test_tabular_strict.cpp): exact free-energy
  polynomials, both table ranks, nonuniform Ye, derivative constraints,
  multiple/flat roots, masked intervals and floating-point boundary failures.

Data-only endpoint references always reside in [fixtures/](../fixtures/README.md). You must keep all backend allocations and kernel launches strictly within their specific test executors; furthermore, always retain each reference's derivation logic and error tolerance whenever extending an existing test case.
