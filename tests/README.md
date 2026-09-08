# Tests

This tree contains focused regression checks and reusable test data. Tests call
the production mathematics; independent reference data and manufactured problems
provide the expected answers. Full scientific campaigns and acceptance records
live in [Validation](../validation/README.md).

## Find a check

- [cuda/](cuda/README.md): real device execution, CPU/CUDA parity, memory and
  transaction lifecycles, and generated-network integration.
- [math/](math/README.md): numerical witnesses shared by host and device tests.
- [fixtures/](fixtures/README.md): reference endpoints and manufactured systems.
- [smoke/](smoke/README.md): short application runs for wiring and output checks.
- [tooling/](tooling/README.md): architecture, CUDA-image and compile-memory tools.
- Top-level `test_*.cpp`: host contracts for policy resolution, AMR plans,
  checkpoint compatibility, state ownership and shared numerical routines.
- Top-level `test_*.py`: validation-runner, provenance, input and resource-guard
  contracts. These test the checking infrastructure, not a physical trajectory.

Target definitions and dependency conditions are in
[CMakeLists.txt](../CMakeLists.txt). Configure with `BUILD_TESTING=ON` and inspect
the configured inventory with `ctest --test-dir <build-dir> -N`; CUDA and optional
solver/network tests depend on the enabled features. Application campaigns that
need generated packages document their setup in the relevant Validation module.

Add a host/device witness once under `math/` or `fixtures/` when both executors
need it. Keep production algorithms in `src/`, not in a test-only replacement.
