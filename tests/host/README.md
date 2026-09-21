# Host regression tests

These C++ tests exercise shared CPU contracts and numerical implementation.
Start with the [checkout verification guide](../README.md); CMake owns target
names and feature conditions.

- Policy and lifecycle checks cover resolved factories, scheduling, boundaries,
  block handles, state residency and topology publication.
- AMR and geometry checks cover conservative transfer, coarse/fine exchange,
  physical measures, refinement indicators and reflux.
- Checkpoint checks use the common HDF5 reader and host restoration entry point.
- Burn, EOS and linear-solver tests use production policies and the reference
  data in [fixtures/](../fixtures/README.md).
- [Generated NSE](network/test_generated_nse.cpp) executes shared analytic witnesses;
  [real generated NSE](network/test_generated_nse_network.cpp) checks the maintained
  `nse_light`/`nse_alpha` packages' detailed balance and three ODE handoffs.
- [Native tabular EOS](eos/NativeTabularRegression.cpp) checks native source axes,
  shifted energy, derivative closure, inverse uniqueness and failure controls.
- [Baryon format](eos/BaryonSourceRegression.cpp) independently checks source units,
  reference alignment, coordinates and malformed input; [real-table EOS](eos/BaryonEosRegression.cpp)
  checks fixed source-node budgets and bounded interior inversions with explicit
  rejection counts. The latter takes table paths and is not a skipped CI test.
- [Helm components](eos/HelmComponentsRegression.cpp) checks the shared isolated
  electron/positron and photon formulas; [component assembly](eos/TabularCompletionRegression.cpp)
  checks both table ranks, component subsets, source mass conventions, metadata,
  single-potential identities and component-dependent fingerprints.

[test_generated_network_reference.cpp](network/test_generated_network_reference.cpp)
is a developer reference-output tool, not a CTest target. It requires an explicit
generated network type and header when compiled; ordinary verification commands
do not need it.

Reusable host/device numerical checks belong in [math/](../math/README.md).
Scientific campaigns are recorded in [Validation](../../validation/README.md).
Tests may provide independent references, but not alternative production solvers.
