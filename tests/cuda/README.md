# CUDA execution and parity tests

These tests exercise production CUDA launchers and compare shared operations
with host execution or independent references. Use the responsibility index
below to find a focused check; CMake defines target names and optional
dependencies centrally.

| Responsibility | Useful entry points |
|---|---|
| Availability, policy and factory selection | [compile probe](runtime/test_cuda_compile_probe.cu), [policy resolution](runtime/test_cuda_policy_resolution.cu), [mainline authority](runtime/test_mainline_authority.cpp) |
| Hydro state, flux and dispatch | [leaf parity](hydro/test_hydro_leaf_parity.cu), [hydro dispatch](hydro/test_hydro_dispatch.cu), [multiblock hydro](hydro/test_cuda_multiblock_hydro.cu) |
| EOS views, ownership and failures | [EOS parity](microphysics/eos/test_eos_host_device_parity.cu), [table owner](microphysics/eos/test_tabular_free_energy_owner.cu), [native table owner](microphysics/eos/test_native_tabular_owner.cu), [completed strict tables](microphysics/eos/test_tabular_completion_device.cu), [hydro failure](hydro/test_hydro_eos_failure.cu), [burn failure](microphysics/burn/test_burn_eos_failure.cu) |
| AMR transactions and migration | [transaction](amr/test_cuda_regrid_transaction.cpp), [migration](amr/test_cuda_regrid_migration.cu), [composition](amr/test_cuda_amr_composition.cu), [store lifecycle](runtime/test_cuda_store_lifecycle.cpp) |
| AMR indicators, ghosts and boundaries | [indicators](amr/test_refinement_indicators.cpp), [exchange](amr/test_cuda_amr_exchange.cpp), [boundary parity](grid/test_boundary_plan_parity.cu) |
| Geometry and diffusion | [metric cache](grid/test_grid_metrics_cache.cu), [geometry witnesses](grid/test_curvilinear_geometry_smoke.cu), [RKL parity](numerics/test_diffusion_rkl_parity.cu), [multiblock diffusion](numerics/test_cuda_multiblock_diffusion.cu) |
| Burn algorithms and controller | [thermal math](microphysics/burn/test_burn_thermal_math.cu), [derivatives](microphysics/network/test_network_derivative.cu), [policy parity](microphysics/burn/test_burn_policy_parity.cu), [controller parity](microphysics/burn/test_burn_controller_parity.cu), [multiblock burn](microphysics/burn/test_cuda_multiblock_burn.cu) |
| Sparse solve and continuation | [cuDSS solver](microphysics/linalg/test_cudss_sparse_solver.cpp), [sparse batch](microphysics/linalg/test_sparse_be_nr_batch.cu), [burn factory](microphysics/linalg/test_cuda_sparse_burn_factory.cpp) |
| Generated networks, weak rates and NSE | [generated math](generated/test_generated_network_math.cu), [sparse integration](generated/test_generated_sparse_burn.cpp), [weak factory](generated/test_generated_weak_factory.cu), [weak trajectory](generated/test_generated_weak_trajectory.cu), [built-in NSE](microphysics/network/test_network_nse_device.cu), [generated NSE witnesses](generated/test_generated_nse_device.cu) |
| Reductions and checkpoint comparison | [compensated sum](numerics/test_compensated_sum.cu), [reduction contract](runtime/test_cuda_reduction_contract.cu), [checkpoint comparator](../host/io/test_cuda_single_level_validation.cpp) |

[CMakeLists.txt](../../CMakeLists.txt) defines the executable targets and optional
dependencies. [Network validation](../../validation/network/README.md) supplies
the generated-package setup. The shared [math witnesses](../math/README.md) and
[fixtures](../fixtures/README.md) own reusable test data.

An individual test checks its stated contract. Full application coverage,
sanitizer results and combined acceptance are recorded in
[Validation](../../validation/README.md).

The generated sparse trajectory harness accepts optional `--storage-cells FIRST SECOND`
and `--pool-cells COUNT`; defaults remain 2/3 cells and a requested pool of 2.
Non-default transcripts identify those controls and retain the same three ODE
methods and numerical budgets. Completed production checks and separate
experimental-provider capacity results are indexed in the
[HPC-CUDA campaign summary](../../validation/backend/results/hpc-cuda-optimization/README.md).
