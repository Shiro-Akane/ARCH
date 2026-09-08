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

[test_generated_network_reference.cpp](test_generated_network_reference.cpp)
is a developer reference-output tool, not a CTest target. It requires an explicit
generated network type and header when compiled; ordinary verification commands
do not need it.

All host/device numerical witnesses explicitly belong in [math/](../math/README.md). Scientific validation campaigns reside in [Validation](../../validation/README.md); remember that this test code absolutely does not define any alternative production algorithms.
