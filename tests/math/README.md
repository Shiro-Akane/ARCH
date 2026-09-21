# Shared numerical witnesses

These headers define reusable test cases for CPU and CUDA executors. Analytic
identities, manufactured states and independently derived reference data check
the production routines without maintaining another solver.

Here a "witness" means a specific input and its expected numerical property,
such as a known derivative or conserved quantity. Sharing these cases lets the
host and device tests ask the same question of the same mathematical routine.

- [BurnThermalCases.h](microphysics/BurnThermalCases.h): thermal closure, Jacobian and rejected
  ODE-trial checks.
- [DenseLuCases.h](DenseLuCases.h) and [NetworkDerivativeCases.h](microphysics/NetworkDerivativeCases.h):
  mixed-scale linear solves and derivative callbacks.
- [CompensatedSumCases.h](CompensatedSumCases.h): cancellation-sensitive sums.
- [GeneratedNseCases.h](microphysics/GeneratedNseCases.h): independent rank-one/rank-two
  equilibrium, fixed points, energy-gauge/thermal closure and rejection controls.
- [CurvilinearMetricCases.h](geometry/CurvilinearMetricCases.h) and
  [ViscousGeometryCases.h](geometry/ViscousGeometryCases.h): physical measures and analytic
  diffusion witnesses.
- [RoeThermodynamicCases.h](RoeThermodynamicCases.h): thermodynamic and flux identities.
- [Strict tabular inversion](microphysics/test_tabular_strict.cpp): exact free-energy
  polynomials, both table ranks, nonuniform Ye, derivative constraints,
  multiple/flat roots, masked intervals and floating-point boundary failures.

Data-only reference values live in [fixtures](../fixtures/README.md). Backend
allocation and kernel launches belong in test executors. When extending a case,
preserve its independent derivation and stated error budget.
